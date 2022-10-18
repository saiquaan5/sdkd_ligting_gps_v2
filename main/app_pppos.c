#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include "rom/queue.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_netif.h"
#include "esp_netif_ppp.h"
#include "mqtt_client.h"
#include "esp_modem.h"
#include "esp_modem_netif.h"
#include "esp_log.h"
#include "sim800.h"
#include "bg96.h"
#include "sim7600.h"
#include "driver/gpio.h"
#include "app_pppos.h"
#include "app_common.h"
static const char* TAG = "APP_PPPOS";

static EventGroupHandle_t event_group = NULL;
static const int CONNECT_BIT = BIT0;
static const int STOP_BIT = BIT1;
// static const int GOT_DATA_BIT = BIT2;


typedef struct app_ppp_ {
    bool run;
    bool is_connected;
    modem_dce_t* dce;
    modem_dte_t *dte;
    void *modem_netif_adapter;
    esp_netif_t *esp_netif;
} app_ppp_t;

void Power_On_Sim()
{
    // gpio_reset_pin(PWR_SIM);
    // gpio_reset_pin(NET_SIM);
    // gpio_reset_pin(S);
    // gpio_reset_pin(S1);
    // gpio_reset_pin(GPS_ACTIVE);
    
    gpio_set_direction(S0_MUX, GPIO_MODE_OUTPUT);
    gpio_set_direction(S1_MUX, GPIO_MODE_OUTPUT);
    // gpio_set_direction(GPS_ACTIVE, GPIO_MODE_OUTPUT);


    gpio_set_direction(PWR_SIM, GPIO_MODE_OUTPUT);
    gpio_set_direction(NET_SIM, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_ACT, GPIO_MODE_OUTPUT);

    gpio_set_level(PWR_SIM, 0); // Bat nguon sim
    // gpio_set_level(GPS_ACTIVE, 0); // Tat nguon GPS
    gpio_set_level(S0_MUX, 1); // chon che do sim
    gpio_set_level(S1_MUX, 0); // chon che do sim

    gpio_set_level(NET_SIM, 1);

    vTaskDelay(2500/portTICK_PERIOD_MS);

    gpio_set_level(NET_SIM, 0);

    vTaskDelay(26000/portTICK_PERIOD_MS);

    gpio_set_level(NET_SIM, 1);

    vTaskDelay(500/portTICK_PERIOD_MS);

    gpio_set_level(NET_SIM, 0);

    vTaskDelay(25000/portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "Bat nguon 4G thanh cong");
}

static void modem_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    switch (event_id) {
    case ESP_MODEM_EVENT_PPP_START:
        ESP_LOGI(TAG, "Modem PPP Started");
        break;
    case ESP_MODEM_EVENT_PPP_STOP:
        ESP_LOGI(TAG, "Modem PPP Stopped");
        xEventGroupSetBits(event_group, STOP_BIT);
        break;
    case ESP_MODEM_EVENT_UNKNOWN:
        ESP_LOGW(TAG, "Unknow line received: %s", (char *)event_data);
        break;
    default:
        break;
    }
}

static void on_ppp_changed(void *arg, esp_event_base_t event_base,
                           int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "PPP state changed event %d", event_id);
    if (event_id == NETIF_PPP_ERRORUSER) {
        /* User interrupted event from esp-netif */
        esp_netif_t *netif = *(esp_netif_t**)event_data;
        ESP_LOGI(TAG, "User interrupted event from netif:%p", netif);
    }
}


static void on_ip_event(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    app_ppp_handle_t handle = arg;
    ESP_LOGD(TAG, "IP event! %d", event_id);
    if (event_id == IP_EVENT_PPP_GOT_IP) {
        handle->is_connected = true;
        esp_netif_dns_info_t dns_info;

        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        esp_netif_t *netif = event->esp_netif;

        ESP_LOGI(TAG, "Modem Connect to PPP Server");
        ESP_LOGI(TAG, "~~~~~~~~~~~~~~");
        ESP_LOGI(TAG, "IP          : " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Netmask     : " IPSTR, IP2STR(&event->ip_info.netmask));
        ESP_LOGI(TAG, "Gateway     : " IPSTR, IP2STR(&event->ip_info.gw));
        esp_netif_get_dns_info(netif, 0, &dns_info);
        ESP_LOGI(TAG, "Name Server1: " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
        esp_netif_get_dns_info(netif, 1, &dns_info);
        ESP_LOGI(TAG, "Name Server2: " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
        ESP_LOGI(TAG, "~~~~~~~~~~~~~~");
        xEventGroupSetBits(event_group, CONNECT_BIT);

        ESP_LOGI(TAG, "GOT ip event!!!");
    } else if (event_id == IP_EVENT_PPP_LOST_IP) {
        handle->is_connected = false;
        ESP_LOGI(TAG, "Modem Disconnect from PPP Server");
    } else if (event_id == IP_EVENT_GOT_IP6) {
        ESP_LOGI(TAG, "GOT IPv6 event!");

        ip_event_got_ip6_t *event = (ip_event_got_ip6_t *)event_data;
        ESP_LOGI(TAG, "Got IPv6 address " IPV6STR, IPV62STR(event->ip6_info.ip));
    }
}

void _ppp_task(void* pv) {

    
    app_ppp_handle_t handle = (app_ppp_handle_t) pv;

    Power_On_Sim();
#if CONFIG_LWIP_PPP_PAP_SUPPORT
    esp_netif_auth_type_t auth_type = NETIF_PPP_AUTHTYPE_PAP;
#elif CONFIG_LWIP_PPP_CHAP_SUPPORT
    esp_netif_auth_type_t auth_type = NETIF_PPP_AUTHTYPE_CHAP;
#elif !defined(CONFIG_EXAMPLE_MODEM_PPP_AUTH_NONE)
#error "Unsupported AUTH Negotiation"
#endif
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &on_ip_event, handle));
    ESP_ERROR_CHECK(esp_event_handler_register(NETIF_PPP_STATUS, ESP_EVENT_ANY_ID, &on_ppp_changed, NULL));

    event_group = xEventGroupCreate();

    /* create dte object */
    esp_modem_dte_config_t config = ESP_MODEM_DTE_DEFAULT_CONFIG();
    /* setup UART specific configuration based on kconfig options */

    config.tx_io_num = CONFIG_EXAMPLE_MODEM_UART_TX_PIN;
    config.rx_io_num = CONFIG_EXAMPLE_MODEM_UART_RX_PIN;
    config.rts_io_num = CONFIG_EXAMPLE_MODEM_UART_RTS_PIN;
    config.cts_io_num = CONFIG_EXAMPLE_MODEM_UART_CTS_PIN;
    config.rx_buffer_size = CONFIG_EXAMPLE_MODEM_UART_RX_BUFFER_SIZE;
    config.tx_buffer_size = CONFIG_EXAMPLE_MODEM_UART_TX_BUFFER_SIZE;
    config.pattern_queue_size = CONFIG_EXAMPLE_MODEM_UART_PATTERN_QUEUE_SIZE;
    config.event_queue_size = CONFIG_EXAMPLE_MODEM_UART_EVENT_QUEUE_SIZE;
    config.event_task_stack_size = CONFIG_EXAMPLE_MODEM_UART_EVENT_TASK_STACK_SIZE;
    config.event_task_priority = CONFIG_EXAMPLE_MODEM_UART_EVENT_TASK_PRIORITY;
    config.line_buffer_size = CONFIG_EXAMPLE_MODEM_UART_RX_BUFFER_SIZE / 2;

    handle->dte = esp_modem_dte_init(&config);
    /* Register event handler */
    ESP_ERROR_CHECK(esp_modem_set_event_handler(handle->dte, modem_event_handler, ESP_EVENT_ANY_ID, NULL));

    // Init netif object
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_PPP();
    handle->esp_netif = esp_netif_new(&cfg);
    assert(handle->esp_netif);

    handle->modem_netif_adapter = esp_modem_netif_setup(handle->dte);
    esp_modem_netif_set_default_handlers(handle->modem_netif_adapter, handle->esp_netif);

    ESP_LOGI(TAG, "reach here %d - %d", CONFIG_EXAMPLE_MODEM_UART_TX_PIN, CONFIG_EXAMPLE_MODEM_UART_RX_PIN);
    handle->is_connected = false;

    while (1) {

        if (handle->is_connected) {
            vTaskDelay(pdMS_TO_TICKS(20000));
            continue;
        }
        handle->dce = NULL;
        /* create dce object */
        handle->dce = sim7600_init(handle->dte);
        if (handle->dce == NULL) {
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }
        ESP_ERROR_CHECK(handle->dce->set_flow_ctrl(handle->dce, MODEM_FLOW_CONTROL_NONE));
        ESP_ERROR_CHECK(handle->dce->store_profile(handle->dce));

        /* Print Module ID, Operator, IMEI, IMSI */
        
        ESP_LOGI(TAG, "Module: %s", handle->dce->name);
        ESP_LOGI(TAG, "Operator: %s", handle->dce->oper);
        ESP_LOGI(TAG, "IMEI: %s", handle->dce->imei);
        ESP_LOGI(TAG, "IMSI: %s", handle->dce->imsi);
        
        /* Get signal quality */
        uint32_t rssi = 0, ber = 0;
        ESP_ERROR_CHECK(handle->dce->get_signal_quality(handle->dce, &rssi, &ber));
        ESP_LOGI(TAG, "rssi: %d, ber: %d", rssi, ber);
        /* Get battery voltage */
        uint32_t voltage = 0, bcs = 0, bcl = 0;
        ESP_ERROR_CHECK(handle->dce->get_battery_status(handle->dce, &bcs, &bcl, &voltage));
        ESP_LOGI(TAG, "Battery voltage: %d mV", voltage);
        /* setup PPPoS network parameters */

#if !defined(CONFIG_EXAMPLE_MODEM_PPP_AUTH_NONE) && (defined(CONFIG_LWIP_PPP_PAP_SUPPORT) || defined(CONFIG_LWIP_PPP_CHAP_SUPPORT))
        esp_netif_ppp_set_auth(handle->esp_netif, auth_type, CONFIG_EXAMPLE_MODEM_PPP_AUTH_USERNAME, CONFIG_EXAMPLE_MODEM_PPP_AUTH_PASSWORD);
#endif

        /* attach the modem to the network interface */
        esp_netif_attach(handle->esp_netif, handle->modem_netif_adapter);
        /* Wait for IP address */
        xEventGroupWaitBits(event_group, CONNECT_BIT, pdTRUE, pdTRUE, portMAX_DELAY);

        /* Config MQTT */
        // esp_mqtt_client_config_t mqtt_config = {
        //     .uri = BROKER_URL,
        //     .event_handle = mqtt_event_handler,
        // };
        // esp_mqtt_client_handle_t mqtt_client = esp_mqtt_client_init(&mqtt_config);
        // esp_mqtt_client_start(mqtt_client);
        // xEventGroupWaitBits(event_group, GOT_DATA_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
        // esp_mqtt_client_destroy(mqtt_client);

        /* Exit PPP mode */
//         ESP_ERROR_CHECK(esp_modem_stop_ppp(handle->dte));

//         xEventGroupWaitBits(event_group, STOP_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
// #if CONFIG_EXAMPLE_SEND_MSG
//         const char *message = "Welcome to ESP32!";
//         ESP_ERROR_CHECK(example_send_message_text(dce, CONFIG_EXAMPLE_SEND_MSG_PEER_PHONE_NUMBER, message));
//         ESP_LOGI(TAG, "Send send message [%s] ok", message);
// #endif

//         /* Power down module */
//         ESP_ERROR_CHECK(dce->power_down(dce));
//         ESP_LOGI(TAG, "Power down");
//         ESP_ERROR_CHECK(dce->deinit(dce));

//         ESP_LOGI(TAG, "Restart after 60 seconds");
//         vTaskDelay(pdMS_TO_TICKS(60000));
    }

    
}

esp_err_t app_ppp_destroy(app_ppp_handle_t handle) {
    /* Power down module */
    ESP_ERROR_CHECK(handle->dce->power_down(handle->dce));
    ESP_LOGI(TAG, "Power down");
    ESP_ERROR_CHECK(handle->dce->deinit(handle->dce));
    /* Unregister events, destroy the netif adapter and destroy its esp-netif instance */
    esp_modem_netif_clear_default_handlers(handle->modem_netif_adapter);
    esp_modem_netif_teardown(handle->modem_netif_adapter);
    esp_netif_destroy(handle->esp_netif);

    ESP_ERROR_CHECK(handle->dte->deinit(handle->dte));

    return ESP_OK;

}

app_ppp_handle_t app_ppp_init(app_ppp_cfg_t* ppp_cfg) {

    app_ppp_handle_t app_ppp = malloc(sizeof(app_ppp_t));

    app_ppp->is_connected = false;

    if (xTaskCreate(_ppp_task, "ppp_task", 1024 * 10, app_ppp, 5, NULL) != pdTRUE) {
        ESP_LOGE(TAG, "Error create console task, memory exhausted?");
        return NULL;
    }
    return app_ppp;
}

bool app_ppp_is_connected(app_ppp_handle_t handle) {
    return handle->is_connected;
}

void app_ppp_get_ip_address(app_ppp_handle_t periph) {
    
}

