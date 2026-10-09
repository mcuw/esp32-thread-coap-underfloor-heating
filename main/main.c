/*
 * This is a undderfloor heating end-device
 * based on OpenThread Command Line Example
 *
*/

#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_types.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "esp_openthread_netif_glue.h"
#include "esp_openthread_types.h"
#include "esp_openthread_lock.h"
#include "esp_ot_config.h"
#include "esp_vfs_eventfd.h"
#include "nvs_flash.h"
#include "ot_examples_common.h"
#include "openthread/thread.h"
#include "openthread/dataset.h"

// WS2812 control component
#if CONFIG_HEATING_MOCK_HW
#include "hw_sim.h"
#if !CONFIG_OPENTHREAD_STATE_INDICATOR_ENABLE
#include "ws2812_control.h"   // nur noetig, wenn der Thread-Indikator die LED nicht besitzt
#endif
#endif

// CoAP underfloor heating component
// (nutzt valve_control -> motor_drv -> ina219; im Mock-Modus hw_sim + RGB-LED)
#include "coap_underfloor_heating.h"

// auto-joiner component
#include "auto_joiner.h"

#if CONFIG_OPENTHREAD_STATE_INDICATOR_ENABLE
#include "ot_led_strip.h"
#endif

#if CONFIG_OPENTHREAD_CLI_ESP_EXTENSION
#include "esp_ot_cli_extension.h"
#endif // CONFIG_OPENTHREAD_CLI_ESP_EXTENSION

#if CONFIG_ESP_COEX_EXTERNAL_COEXIST_ENABLE
#include "ext_coex_cmd.h"
#endif

#define TAG "main"

#if CONFIG_HEATING_MOCK_HW
// LED-Ausgabe der Simulation: es gibt nur EINEN LED-Treiber.
// Mit Thread-Indikator wird dessen Treiber mitbenutzt, sonst ws2812_control.
static void sim_led(bool on, uint8_t r, uint8_t g, uint8_t b)
{
#if CONFIG_OPENTHREAD_STATE_INDICATOR_ENABLE
    if (on) {
        esp_openthread_state_indicator_set(0, r, g, b);
    } else {
        esp_openthread_state_indicator_clear();
    }
#else
    ws2812_control_set(on, r, g, b);
#endif
}
#endif

void app_main(void)
{
    // Used eventfds:
    // * netif
    // * ot task queue
    // * radio driver
    esp_vfs_eventfd_config_t eventfd_config = {
        .max_fds = 3,
    };

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
#if CONFIG_OPENTHREAD_PLATFORM_NETIF
    ESP_ERROR_CHECK(esp_netif_init());
#endif
    ESP_ERROR_CHECK(esp_vfs_eventfd_register(&eventfd_config));

#if CONFIG_OPENTHREAD_CLI
    ot_console_start();
    ot_register_external_commands();
#if CONFIG_ESP_COEX_EXTERNAL_COEXIST_ENABLE
    register_cmd_extcoex();
#endif
#endif

    static esp_openthread_config_t config = {
#if CONFIG_OPENTHREAD_PLATFORM_NETIF
        .netif_config = ESP_NETIF_DEFAULT_OPENTHREAD(),
#endif
        .platform_config = {
            .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
            .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
            .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
        },
    };

    ESP_ERROR_CHECK(esp_openthread_start(&config));

    // autostart the OpenThread network if enabled in menuconfig
    #if CONFIG_OPENTHREAD_CLI_ESP_EXTENSION
        esp_cli_custom_command_init();
    #endif

    // Auto-Attach: Falls bereits ein Dataset aus einer frueheren Session
    // persistiert ist, automatisch dem Netzwerk beitreten - kein manuelles
    // ifconfig/joiner/thread-start mehr noetig nach einem Reboot.
    if (esp_openthread_lock_acquire(pdMS_TO_TICKS(1000))) {
        otInstance *instance = esp_openthread_get_instance();
        if (otDatasetIsCommissioned(instance)) {
            otError err1 = otIp6SetEnabled(instance, true);
            otError err2 = otThreadSetEnabled(instance, true);
            if (err1 == OT_ERROR_NONE && err2 == OT_ERROR_NONE) {
                ESP_LOGI(TAG, "Auto-attach: persistiertes Dataset gefunden, Thread gestartet");
            } else {
                ESP_LOGW(TAG, "Auto-attach fehlgeschlagen (ip6: %d, thread: %d)", err1, err2);
            }
        } else {
            auto_joiner_init();
        }
        esp_openthread_lock_release();
    }


    //
    // LED: genau ein Treiber, VOR dem Heizungs-Init, weil die Simulation ihn nutzt.
    //  - Thread-Indikator aktiv: dessen led_strip-Treiber (ot_led_strip.c)
    //  - sonst im Mock-Modus: ws2812_control
    //
#if CONFIG_OPENTHREAD_STATE_INDICATOR_ENABLE
    ESP_ERROR_CHECK(esp_openthread_state_indicator_init(esp_openthread_get_instance()));
#elif CONFIG_HEATING_MOCK_HW
    ws2812_control_init();
#endif

    //
    // CoAP underfloor heating
    // Zonenregelung per CoAP: /heating, /heating/temp, /heating/setpoint
    // Startet valve_control, motor_drv und ina219 (oder deren Simulation,
    // siehe menuconfig -> Underfloor Heating -> "Hardware simulieren").
    //
#if CONFIG_HEATING_MOCK_HW
    hw_sim_set_led(sim_led);
    ESP_LOGW(TAG, "MOCK-Modus aktiv: Motoren/INA219 werden simuliert, "
                  "RGB-LED zeigt Fahrbewegung (rot=auf, blau=zu, weiss=Anschlag)");
#endif
    coap_underfloor_heating_init();

#if CONFIG_OPENTHREAD_NETWORK_AUTO_START
    ot_network_auto_start();
#endif
}
