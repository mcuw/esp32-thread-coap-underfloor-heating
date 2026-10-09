#include "sdkconfig.h"
#include "ina219.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

static const char *TAG = "ina219";

#if CONFIG_HEATING_MOCK_HW
// ---- MOCK: Strom kommt aus der Motorsimulation ------------------------------
#include "hw_sim.h"

esp_err_t ina219_init(void)
{
    ESP_LOGW(TAG, "MOCK: simulierter INA219");
    return ESP_OK;
}

esp_err_t ina219_read_current_ma(float *ma)
{
    *ma = hw_sim_current_ma();
    return ESP_OK;
}

#else
// ---- Echte Hardware ---------------------------------------------------------
// ---- Anpassen -------------------------------------------------------------
#define INA219_PIN_SDA     GPIO_NUM_22
#define INA219_PIN_SCL     GPIO_NUM_23
#define INA219_ADDR        0x40
#define INA219_SHUNT_OHM   0.1f     // Standard-Modul: R100 = 0,1 Ohm
#define INA219_I2C_HZ      400000

// Konfiguration: BRNG 16 V, PGA /2 (+-80 mV -> +-800 mA bei 0,1 Ohm),
// 12 Bit / 532 us Wandlung, kontinuierlich Shunt + Bus
#define INA219_CONFIG      0x099F

#define REG_CONFIG         0x00
#define REG_SHUNT_VOLTAGE  0x01     // LSB = 10 uV, vorzeichenbehaftet

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;

static esp_err_t write_reg(uint8_t reg, uint16_t val)
{
    uint8_t buf[3] = { reg, (uint8_t)(val >> 8), (uint8_t)(val & 0xFF) };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
}

static esp_err_t read_reg(uint8_t reg, uint16_t *val)
{
    uint8_t rx[2];
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, rx, sizeof(rx), 100);
    if (err == ESP_OK) *val = ((uint16_t)rx[0] << 8) | rx[1];
    return err;
}

esp_err_t ina219_init(void)
{
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = INA219_PIN_SDA,
        .scl_io_num = INA219_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C-Bus: %s", esp_err_to_name(err));
        return err;
    }

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = INA219_ADDR,
        .scl_speed_hz = INA219_I2C_HZ,
    };
    err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C-Device: %s", esp_err_to_name(err));
        i2c_del_master_bus(s_bus);
        return err;
    }

    err = write_reg(REG_CONFIG, INA219_CONFIG);
    uint16_t check = 0;
    if (err == ESP_OK) err = read_reg(REG_CONFIG, &check);
    if (err != ESP_OK || check != INA219_CONFIG) {
        ESP_LOGE(TAG, "INA219 nicht gefunden/Config falsch (%s, 0x%04X)",
                 esp_err_to_name(err), check);
        i2c_master_bus_rm_device(s_dev);
        i2c_del_master_bus(s_bus);
        return err != ESP_OK ? err : ESP_FAIL;
    }

    ESP_LOGI(TAG, "INA219 bereit (0x%02X, %.3f Ohm)", INA219_ADDR, INA219_SHUNT_OHM);
    return ESP_OK;
}

esp_err_t ina219_read_current_ma(float *ma)
{
    uint16_t raw;
    esp_err_t err = read_reg(REG_SHUNT_VOLTAGE, &raw);
    if (err != ESP_OK) return err;
    // raw * 10 uV = raw * 0.01 mV;  mV / Ohm = mA
    *ma = (float)(int16_t)raw * 0.01f / INA219_SHUNT_OHM;
    return ESP_OK;
}

#endif // CONFIG_HEATING_MOCK_HW
