#include <string.h>
#include "sdkconfig.h"
#include "motor_drv.h"
#include "ina219.h"
#if CONFIG_HEATING_MOCK_HW
#include "hw_sim.h"
#endif
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "motor_drv";

// ---- Pinbelegung anpassen -------------------------------------------------
#define PIN_S0     GPIO_NUM_0    // S0..S3 an BEIDEN HC4067
#define PIN_S1     GPIO_NUM_1
#define PIN_S2     GPIO_NUM_2
#define PIN_S3     GPIO_NUM_3
#define PIN_SIG_A  GPIO_NUM_10   // U1.SIG -> AIA der L9110-Kanaele
#define PIN_SIG_B  GPIO_NUM_11   // U2.SIG -> AIB der L9110-Kanaele
#define PIN_EN     GPIO_NUM_4    // /E beider Muxe, active-low

// ---- Anschlagserkennung ueber Strom ---------------------------------------
#define SAMPLE_MS           20      // Abtastintervall
#define INRUSH_MS           500     // Anlaufstoss ignorieren
#define BASELINE_MS         400     // danach: Fahrstrom als Referenz messen
#define STALL_REL           1.6f    // Anschlag, wenn Strom > Referenz * Faktor ...
#define STALL_MIN_DELTA_MA  40.0f   // ... UND mindestens so viel mA darueber
#define STALL_ABS_MA        0.0f    // >0: zusaetzlich absolute Schwelle in mA
#define STALL_SAMPLES       3       // so viele Messungen in Folge
#define FILTER_ALPHA        0.4f    // Glaettung (0..1)

static bool s_sense_ok;

void motor_drv_stop(void)
{
#if CONFIG_HEATING_MOCK_HW
    hw_sim_motor_stop();
#else
    gpio_set_level(PIN_EN, 1);       // zuerst trennen
    gpio_set_level(PIN_SIG_A, 0);
    gpio_set_level(PIN_SIG_B, 0);
#endif
}

bool motor_drv_has_sense(void) { return s_sense_ok; }

void motor_drv_init(void)
{
#if CONFIG_HEATING_MOCK_HW
    hw_sim_init();                   // keine GPIOs im Mock-Modus
#else
    const gpio_num_t pins[] = { PIN_S0, PIN_S1, PIN_S2, PIN_S3,
                                PIN_SIG_A, PIN_SIG_B, PIN_EN };
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        gpio_reset_pin(pins[i]);
        gpio_set_direction(pins[i], GPIO_MODE_OUTPUT);
    }
#endif
    motor_drv_stop();

    s_sense_ok = (ina219_init() == ESP_OK);
    if (!s_sense_ok) {
        ESP_LOGW(TAG, "Keine Strommessung -> nur zeitgesteuert (Fallback)");
    }
}

// Kanal waehlen und Richtung ausgeben; nur ein Motor gleichzeitig.
// Belegung wie im Arduino-Test:
//   oeffnen (waermer):    AIA=0, AIB=1
//   schliessen (kaelter): AIA=1, AIB=0
static void run(uint8_t ch, motor_dir_t dir)
{
    motor_drv_stop();
    if (dir == MOTOR_STOP || ch > 15) return;

#if CONFIG_HEATING_MOCK_HW
    hw_sim_motor_run(ch, dir == MOTOR_OPEN ? +1 : -1);
    return;
#endif

    gpio_set_level(PIN_S0, ch & 1);
    gpio_set_level(PIN_S1, (ch >> 1) & 1);
    gpio_set_level(PIN_S2, (ch >> 2) & 1);
    gpio_set_level(PIN_S3, (ch >> 3) & 1);

    gpio_set_level(PIN_SIG_A, dir == MOTOR_CLOSE ? 1 : 0);
    gpio_set_level(PIN_SIG_B, dir == MOTOR_OPEN  ? 1 : 0);

    gpio_set_level(PIN_EN, 0);       // Kanal aktiv
}

// Ruhestrom (Module, INA219-Offset) bei stehendem Motor messen
static float measure_idle_offset(void)
{
    float sum = 0;
    int n = 0;
    for (int i = 0; i < 5; i++) {
        float ma;
        if (ina219_read_current_ma(&ma) == ESP_OK) { sum += ma; n++; }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return n ? sum / n : 0.0f;
}

void motor_drv_move(uint8_t channel, motor_dir_t dir, uint32_t max_ms,
                    motor_result_t *res)
{
    motor_result_t r;
    memset(&r, 0, sizeof(r));

    float offset = s_sense_ok ? measure_idle_offset() : 0.0f;

    float    filt = 0, baseline = 0, base_sum = 0;
    int      base_n = 0, over = 0;
    bool     filt_init = false;
    uint32_t elapsed = 0;
    int64_t  t0 = esp_timer_get_time();

    run(channel, dir);

    while (elapsed < max_ms) {
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_MS));
        elapsed = (uint32_t)((esp_timer_get_time() - t0) / 1000);

        if (!s_sense_ok || elapsed < INRUSH_MS) continue;

        float ma;
        if (ina219_read_current_ma(&ma) != ESP_OK) continue;
        ma -= offset;

        if (!filt_init) { filt = ma; filt_init = true; }
        else            { filt += FILTER_ALPHA * (ma - filt); }
        if (filt > r.peak_ma) r.peak_ma = filt;

        bool in_baseline = elapsed < (INRUSH_MS + BASELINE_MS);
        if (in_baseline) {
            base_sum += ma;
            base_n++;
            baseline = base_sum / base_n;
        }

        bool hit = false;
        if (STALL_ABS_MA > 0.0f && filt >= STALL_ABS_MA) hit = true;
        if (!in_baseline && baseline > 0.0f &&
            filt >= baseline * STALL_REL &&
            (filt - baseline) >= STALL_MIN_DELTA_MA) hit = true;

        over = hit ? over + 1 : 0;
        if (over >= STALL_SAMPLES) {
            r.stalled = true;
            break;
        }
    }

    motor_drv_stop();

    r.elapsed_ms = elapsed;
    r.run_ma = baseline;
    *res = r;
}
