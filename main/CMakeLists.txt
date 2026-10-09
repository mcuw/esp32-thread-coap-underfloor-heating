idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_netif esp_event openthread nvs_flash coap_underfloor_heating auto_joiner)
