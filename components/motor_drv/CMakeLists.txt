idf_component_register(
    SRCS "src/motor_drv.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_driver_gpio esp_timer ina219 hw_sim
    REQUIRES esp_common
)