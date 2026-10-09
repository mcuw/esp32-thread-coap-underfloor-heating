idf_component_register(
    SRCS "src/coap_underfloor_heating.c"
    INCLUDE_DIRS "include"
    REQUIRES openthread ws2812_control espressif__cjson valve_control
)