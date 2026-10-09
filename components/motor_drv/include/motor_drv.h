#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { MOTOR_STOP, MOTOR_OPEN, MOTOR_CLOSE } motor_dir_t;

typedef struct {
    bool     stalled;      // Anschlag ueber Stromanstieg erkannt
    uint32_t elapsed_ms;   // tatsaechliche Fahrzeit
    float    run_ma;       // Fahrstrom (Mittelwert nach dem Anlaufstoss)
    float    peak_ma;      // Spitzenwert (gefiltert)
} motor_result_t;

void motor_drv_init(void);
bool motor_drv_has_sense(void);          // INA219 vorhanden?
void motor_drv_stop(void);

// Faehrt EINEN Motor (blockierend) bis Anschlag (Strom) oder max_ms.
void motor_drv_move(uint8_t channel, motor_dir_t dir, uint32_t max_ms,
                    motor_result_t *res);
