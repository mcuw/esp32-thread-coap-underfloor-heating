idf_component_register(
    SRCS "src/valve_control.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_timer motor_drv
)