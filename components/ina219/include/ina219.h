#pragma once
#include "esp_err.h"

// INA219 als High-Side-Strommesser in der 3,3-V-Versorgung der L9110-Module.
// Es wird direkt das Shunt-Spannungsregister gelesen (kein Kalibrierregister noetig).

esp_err_t ina219_init(void);
esp_err_t ina219_read_current_ma(float *ma);
