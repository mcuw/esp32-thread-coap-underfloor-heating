#pragma once
#include <stdbool.h>
#include <stdint.h>

// Simulation der Hardware (Mux + L9110 + Motor + INA219) fuer Tests ohne Aufbau.
// Nur verfuegbar mit CONFIG_HEATING_MOCK_HW=y.
// Anzeige ueber die RGB-LED:  rot = oeffnen, blau = schliessen,
// kurzes weisses Aufleuchten = Anschlag erreicht, aus = Stillstand.

// LED-Ausgabe wird von aussen eingehaengt (z. B. Thread-Indikator oder ws2812_control),
// damit hw_sim keinen eigenen LED-Treiber braucht. Ohne Hook: keine LED-Anzeige.
typedef void (*hw_sim_led_fn_t)(bool on, uint8_t r, uint8_t g, uint8_t b);
void  hw_sim_set_led(hw_sim_led_fn_t fn);

void  hw_sim_init(void);

// dir: +1 = oeffnen, -1 = schliessen
void  hw_sim_motor_run(uint8_t channel, int dir);
void  hw_sim_motor_stop(void);

// Simulierter Versorgungsstrom (wie ihn der INA219 messen wuerde) in mA
float hw_sim_current_ma(void);
