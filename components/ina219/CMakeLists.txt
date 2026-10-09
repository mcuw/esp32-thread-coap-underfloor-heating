idf_component_register(
    SRCS "src/ina219.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_driver_i2c esp_driver_gpio hw_sim
    REQUIRES esp_common
)