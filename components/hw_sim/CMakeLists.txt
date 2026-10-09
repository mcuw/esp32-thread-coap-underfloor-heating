idf_component_register(
    SRCS "src/hw_sim.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES esp_timer)