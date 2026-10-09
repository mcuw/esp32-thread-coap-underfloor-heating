#include <math.h>
#include "sdkconfig.h"
#include "valve_control.h"
#include "motor_drv.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "valve_control";

// ---- Konfiguration --------------------------------------------------------
#define SENSOR_TIMEOUT_S        (15 * 60)  // keine Messung -> Failsafe
#define FAILSAFE_VALVE_OPEN     0          // 0 = bei Sensorausfall schliessen
#define DEFAULT_SETPOINT        21.0f
#define HYSTERESIS              0.3f       // +/- um Sollwert
#define TIMEOUT_CHECK_MS        10000

#define OVERRUN_MS              3000       // Nachlauf NUR am offenen Anschlag
#define MIN_STEP_PCT            2.0f
#define MOTOR_POLL_MS           500

// Zeitlimit beim Schliessen, relativ zu travel_ms:
//   mit INA219: etwas mehr, damit der Ventilsitz per Strom erkannt wird
//   ohne INA219: kuerzer, damit der Stoessel sicher nicht zu tief faehrt
#define CLOSE_LIMIT_SENSE_PCT   110
#define CLOSE_LIMIT_BLIND_PCT   96

typedef struct {
    uint8_t     id;          // Zonen-ID, die der Sensor mitsendet
    uint8_t     channel;     // HC4067-Kanal 0..15
    uint32_t    travel_ms;   // offen -> Ventilsitz, pro Zone messen
    float       setpoint;
    float       temp;
    bool        temp_valid;
    bool        valve_open;  // Sollzustand (Zweipunktregler)
    bool        homed;       // Referenzfahrt (offen) erfolgt?
    float       pos;         // 100 = offen (eingefahren), 0 = zu
    float       target;      // Zielposition
    int64_t     last_update_us;
} zone_t;

// Fahrzeit pro Zone (offen -> Ventilsitz). Im Mock-Modus gilt die simulierte
// Fahrzeit aus menuconfig, damit Regelung und Simulation zusammenpassen.
#if CONFIG_HEATING_MOCK_HW
#define TRAVEL_Z1  CONFIG_HEATING_MOCK_TRAVEL_MS
#define TRAVEL_Z2  CONFIG_HEATING_MOCK_TRAVEL_MS
#define TRAVEL_Z3  CONFIG_HEATING_MOCK_TRAVEL_MS
#define TRAVEL_Z4  CONFIG_HEATING_MOCK_TRAVEL_MS
#else
#define TRAVEL_Z1  30000     // echte Werte pro Zone messen
#define TRAVEL_Z2  30000
#define TRAVEL_Z3  30000
#define TRAVEL_Z4  30000
#endif

// Hier Zonen anlegen: ID, Mux-Kanal, Fahrzeit
static zone_t s_zones[] = {
    { .id = 1, .channel = 0, .travel_ms = TRAVEL_Z1 },
    { .id = 2, .channel = 1, .travel_ms = TRAVEL_Z2 },
    { .id = 3, .channel = 2, .travel_ms = TRAVEL_Z3 },
    { .id = 4, .channel = 3, .travel_ms = TRAVEL_Z4 },
};
#define ZONE_COUNT (sizeof(s_zones) / sizeof(s_zones[0]))

static SemaphoreHandle_t s_mutex;

static zone_t *find_zone(uint8_t id)
{
    for (size_t i = 0; i < ZONE_COUNT; i++) {
        if (s_zones[i].id == id) return &s_zones[i];
    }
    return NULL;
}

// Mutex muss gehalten werden
static void apply_valve(zone_t *z, bool open)
{
    if (z->valve_open == open) return;
    z->valve_open = open;
    z->target = open ? 100.0f : 0.0f;
    ESP_LOGI(TAG, "Zone %u (Kanal %u): Ventil -> %s (T=%.1f, Soll=%.1f)",
             z->id, z->channel, open ? "AUF" : "ZU", z->temp, z->setpoint);
}

// Zweipunktregler mit Hysterese (Mutex muss gehalten werden)
static void evaluate(zone_t *z)
{
    if (!z->temp_valid) {
        apply_valve(z, FAILSAFE_VALVE_OPEN);
        return;
    }
    if (z->temp <= z->setpoint - HYSTERESIS) {
        apply_valve(z, true);
    } else if (z->temp >= z->setpoint + HYSTERESIS) {
        apply_valve(z, false);
    }
    // dazwischen: Zustand halten
}

static void timeout_cb(void *arg)
{
    int64_t now = esp_timer_get_time();
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (size_t i = 0; i < ZONE_COUNT; i++) {
        zone_t *z = &s_zones[i];
        if (z->temp_valid &&
            (now - z->last_update_us) > (int64_t)SENSOR_TIMEOUT_S * 1000000) {
            ESP_LOGW(TAG, "Zone %u: Sensor-Timeout", z->id);
            z->temp_valid = false;
            evaluate(z);
        }
    }
    xSemaphoreGive(s_mutex);
}

// Faehrt immer nur einen Motor gleichzeitig.
// Richtung OFFEN (Stoessel eingefahren) ist die sichere Referenz: dort darf
// in den Anschlag gefahren werden. Richtung ZU nie ueber das Limit hinaus.
static void motor_task(void *arg)
{
    for (;;) {
        zone_t *z = NULL;
        bool home = false;
        float from = 0, to = 0;

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        // 1. Referenzfahrt hat Vorrang
        for (size_t i = 0; i < ZONE_COUNT; i++) {
            if (!s_zones[i].homed) { z = &s_zones[i]; home = true; break; }
        }
        // 2. sonst Zielposition anfahren
        if (!z) {
            for (size_t i = 0; i < ZONE_COUNT; i++) {
                if (fabsf(s_zones[i].target - s_zones[i].pos) >= MIN_STEP_PCT) {
                    z = &s_zones[i];
                    break;
                }
            }
        }
        if (z) {
            from = home ? 0.0f : z->pos;
            to   = home ? 100.0f : z->target;
        }
        xSemaphoreGive(s_mutex);

        if (!z) {
            vTaskDelay(pdMS_TO_TICKS(MOTOR_POLL_MS));
            continue;
        }

        motor_dir_t dir = (to > from) ? MOTOR_OPEN : MOTOR_CLOSE;
        uint32_t max_ms = (uint32_t)(fabsf(to - from) / 100.0f * z->travel_ms);

        if (dir == MOTOR_OPEN) {
            if (to >= 100.0f) max_ms += OVERRUN_MS;       // sicher in den Anschlag
        } else {
            max_ms = max_ms * (motor_drv_has_sense() ? CLOSE_LIMIT_SENSE_PCT
                                                     : CLOSE_LIMIT_BLIND_PCT) / 100;
        }

        ESP_LOGI(TAG, "Zone %u: %s %.0f%% -> %.0f%% (max %u ms)", z->id,
                 home ? "Referenzfahrt" : "fahre", from, to, (unsigned)max_ms);

        motor_result_t r;
        motor_drv_move(z->channel, dir, max_ms, &r);

        float end_pos = to;
        if (r.stalled) {
            // Anschlag erreicht: Position neu synchronisieren
            end_pos = (dir == MOTOR_OPEN) ? 100.0f : 0.0f;
        }

        ESP_LOGI(TAG, "Zone %u: fertig nach %u ms, Anschlag=%s, Fahrstrom=%.0f mA, "
                      "Spitze=%.0f mA -> pos=%.0f%%",
                 z->id, (unsigned)r.elapsed_ms, r.stalled ? "ja" : "nein",
                 r.run_ma, r.peak_ma, end_pos);

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        z->pos = end_pos;
        z->homed = true;
        xSemaphoreGive(s_mutex);
    }
}

void valve_control_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    motor_drv_init();

    for (size_t i = 0; i < ZONE_COUNT; i++) {
        zone_t *z = &s_zones[i];
        z->setpoint   = DEFAULT_SETPOINT;
        z->valve_open = FAILSAFE_VALVE_OPEN;
        z->target     = FAILSAFE_VALVE_OPEN ? 100.0f : 0.0f;
        z->pos        = 0.0f;
        z->homed      = false;   // erst Referenzfahrt (offen), dann Ziel anfahren
    }

    xTaskCreate(motor_task, "motor", 4096, NULL, 4, NULL);

    const esp_timer_create_args_t args = { .callback = timeout_cb, .name = "valve_to" };
    esp_timer_handle_t t;
    ESP_ERROR_CHECK(esp_timer_create(&args, &t));
    ESP_ERROR_CHECK(esp_timer_start_periodic(t, TIMEOUT_CHECK_MS * 1000ULL));
}

size_t valve_control_zone_count(void) { return ZONE_COUNT; }

bool valve_control_update_temp(uint8_t zone, float temp)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    zone_t *z = find_zone(zone);
    if (z) {
        z->temp = temp;
        z->temp_valid = true;
        z->last_update_us = esp_timer_get_time();
        evaluate(z);
    }
    xSemaphoreGive(s_mutex);
    return z != NULL;
}

bool valve_control_set_setpoint(uint8_t zone, float setpoint)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    zone_t *z = find_zone(zone);
    if (z) {
        z->setpoint = setpoint;
        evaluate(z);
    }
    xSemaphoreGive(s_mutex);
    return z != NULL;
}

bool valve_control_get(size_t index, zone_status_t *out)
{
    if (index >= ZONE_COUNT) return false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    const zone_t *z = &s_zones[index];
    out->zone = z->id;
    out->temp = z->temp;
    out->setpoint = z->setpoint;
    out->temp_valid = z->temp_valid;
    out->valve_open = z->valve_open;
    out->pos = z->pos;
    xSemaphoreGive(s_mutex);
    return true;
}
