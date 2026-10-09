#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t zone;
    float   temp;
    float   setpoint;
    bool    temp_valid;   // false = noch nie / Timeout
    bool    valve_open;   // Sollzustand
    float   pos;          // geschaetzte Position: 100 = offen, 0 = zu
} zone_status_t;

void   valve_control_init(void);
size_t valve_control_zone_count(void);
bool   valve_control_update_temp(uint8_t zone, float temp);
bool   valve_control_set_setpoint(uint8_t zone, float setpoint);
bool   valve_control_get(size_t index, zone_status_t *out);
