#include "sdkconfig.h"

#if CONFIG_HEATING_MOCK_HW

#include <stdbool.h>
#include <stdlib.h>
#include "hw_sim.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "hw_sim";

#define SIM_CHANNELS    16
#define SIM_TRAVEL_MS   CONFIG_HEATING_MOCK_TRAVEL_MS   // 0..100 % in dieser Zeit

// Strommodell
#define SIM_RUN_MA      120.0f   // Fahrstrom
#define SIM_INRUSH_MA   300.0f   // Anlaufstoss
#define SIM_INRUSH_MS   200
#define SIM_STALL_MA    280.0f   // im Anschlag
#define SIM_IDLE_MA     1.5f     // Ruhestrom der Module

#define LED_LEVEL       80

static float   s_pos[SIM_CHANNELS];   // 100 = offen (eingefahren), 0 = zu
static uint8_t s_ch;
static int     s_dir;                 // 0 stop, +1 oeffnen, -1 schliessen
static bool    s_blocked;             // laeuft gegen Anschlag
static int64_t s_last_us, s_start_us;

static hw_sim_led_fn_t s_led_fn;

void hw_sim_set_led(hw_sim_led_fn_t fn)
{
    s_led_fn = fn;
}

static void led(bool on, uint8_t r, uint8_t g, uint8_t b)
{
    if (s_led_fn) s_led_fn(on, r, g, b);
}

// Position anhand der vergangenen Zeit nachfuehren
static void integrate(void)
{
    int64_t now = esp_timer_get_time();
    if (s_dir != 0) {
        float d = (float)(now - s_last_us) / 1000.0f / SIM_TRAVEL_MS * 100.0f;
        float p = s_pos[s_ch] + (s_dir > 0 ? d : -d);
        if (p > 100.0f) p = 100.0f;
        if (p < 0.0f)   p = 0.0f;
        s_pos[s_ch] = p;
        s_blocked = (s_dir > 0 && p >= 100.0f) || (s_dir < 0 && p <= 0.0f);
    }
    s_last_us = now;
}

void hw_sim_init(void)
{
    for (int i = 0; i < SIM_CHANNELS; i++) s_pos[i] = 50.0f;   // Position unbekannt
    s_dir = 0;
    s_blocked = false;
    s_last_us = esp_timer_get_time();

    led(false, 0, 0, 0);
    ESP_LOGW(TAG, "MOCK-MODUS: Hardware wird simuliert (%d ms pro Vollhub)",
             SIM_TRAVEL_MS);
}

void hw_sim_motor_run(uint8_t channel, int dir)
{
    if (channel >= SIM_CHANNELS || dir == 0) return;
    hw_sim_motor_stop();

    s_ch = channel;
    s_dir = dir;
    s_blocked = false;
    s_last_us = s_start_us = esp_timer_get_time();

    ESP_LOGI(TAG, "Kanal %u: %s ab %.0f%%", channel,
             dir > 0 ? "OEFFNEN" : "SCHLIESSEN", s_pos[channel]);
    led(true, dir > 0 ? LED_LEVEL : 0, 0, dir < 0 ? LED_LEVEL : 0);
}

void hw_sim_motor_stop(void)
{
    if (s_dir == 0) return;
    integrate();

    ESP_LOGI(TAG, "Kanal %u: Stopp bei %.0f%%%s", s_ch, s_pos[s_ch],
             s_blocked ? " (Anschlag)" : "");
    if (s_blocked) {
        led(true, LED_LEVEL, LED_LEVEL, LED_LEVEL);        // weisses Aufblitzen
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    s_dir = 0;
    s_blocked = false;
    led(false, 0, 0, 0);
}

float hw_sim_current_ma(void)
{
    integrate();
    float noise = (float)(rand() % 100 - 50) / 10.0f;      // +-5 mA

    if (s_dir == 0) return SIM_IDLE_MA + noise * 0.1f;

    int64_t run_ms = (esp_timer_get_time() - s_start_us) / 1000;
    float base = s_blocked ? SIM_STALL_MA
                           : (run_ms < SIM_INRUSH_MS ? SIM_INRUSH_MA : SIM_RUN_MA);
    return base + noise;
}

#endif // CONFIG_HEATING_MOCK_HW
