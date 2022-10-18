/* PPPoS Client Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
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
#include "app_network.h"
#include "app_websocket.h"
#include "app_pppos.h"
#include "app_ota.h"
#include "app_common.h"
#include "app_modbus.h"
#include "app_nextion.h"
#include "app_schedule.h"
#include "app_storage.h"
#include "app_controller.h"

#include "app_gps.h"

#define ENABLE_NETWORK
#define ENABLE_SCHEDULE
#define ENABLE_OTA
// #define ENABLE_MODBUS
#define ENABLE_NEXTION
#define ENABLE_WEBSOCKET
#define ENABLE_GPS

#define TAG "APP_MAIN"

static app_schedule_handle_t schedule_handle = NULL;
static app_storage_handle_t storage_handle = NULL;
static app_nextion_handle_t nextion_handle = NULL;
static app_network_handle_t network_handle = NULL;
static app_controller_handle_t controller_handle = NULL;
static app_websocket_handle_t websocket_handle = NULL;
static app_ota_handle_t ota_handle = NULL;
static app_gps_handle_t gps_handle = NULL;

void event_callback(int event_id, void* data, int data_len) {
    switch (event_id)
    {
    case WEBSOCKET_ON_CONNECTED:
        ESP_LOGD(TAG, "websocket connected");
        app_nextion_set_conection_state(nextion_handle, false);
        app_nextion_set_loading(nextion_handle, 66, "Kết nối máy chủ thành công");
        break;
    case WEBSOCKET_ON_DISCONNECTED:
        ESP_LOGD(TAG, "websocket disconnected");
        app_nextion_set_conection_state(nextion_handle, false);
        break;
    case WEBSOCKET_ON_ONLINE:
        ESP_LOGD(TAG, "device is online");
        app_nextion_set_conection_state(nextion_handle, true);
        app_nextion_set_loading(nextion_handle, 100, "Thiết bị đã online");
        vTaskDelay(500 / portTICK_PERIOD_MS);
        app_nextion_set_page(nextion_handle, PAGE_LOCK_INDEX);
        break;
    case WEBSOCKET_ON_CONTROL:
        if (data == NULL) {
            return;
        }
        bool* is_on = (bool*)data;
        ESP_LOGD(TAG, "Has a control %d", (int)(*is_on));
        if (app_controller_get_mode(controller_handle) == APP_CONTROLLER_INPUT_MODE_OFF) {
            return;
        }
        app_controller_force_manual(controller_handle, false);
        app_controller_control_relay(controller_handle, *is_on);
        app_nextion_set_button(nextion_handle, *is_on);
        app_nextion_set_console(nextion_handle, *is_on ? "Điều khiển bật từ phần mềm" : "Điều khiển tắt từ phần mềm");
        break;
    case WEBSOCKET_ON_FREE:
        ESP_LOGD(TAG, "Free operator mode");
        app_controller_force_manual(controller_handle, true);
        break;
    case WEBSOCKET_ON_FORCE_OTA:
        ESP_LOGD(TAG, "Force OTA");
        app_ota_force_update(ota_handle);
        break;
    case WEBSOCKET_ON_SETTING_ACTIVE_TIME:
        if (data == NULL || data_len != sizeof(active_time_t)) {
            return;
        }
        ESP_LOGD(TAG, "Has a setting active time");
        active_time_t* active_time = (active_time_t*)data;

        app_storage_data_t storage_data = {
            .start_hour = active_time->start_hour,
            .start_minute = active_time->start_minute,
            .enable_start = true,
            .end_hour = active_time->end_hour,
            .end_minute = active_time->end_minute,
            .enable_end = true
        };
        app_storage_set_data(storage_handle, &storage_data);
        app_storage_save_data(storage_handle);
        app_schedule_add_alarm(schedule_handle, storage_data.start_hour, storage_data.start_minute, SCHEDULE_TYPE_ACTIVE_START); 
        app_schedule_add_alarm(schedule_handle, storage_data.end_hour, storage_data.end_minute, SCHEDULE_TYPE_ACTIVE_END);

        app_nextion_set_active_time(nextion_handle, active_time);

        app_websocket_update_active_time(websocket_handle, *active_time);

        break;
    case APP_NEXTION_EVENT_SET_TIME: {
        ESP_LOGD(TAG, "Set time");
        if (data == NULL || data_len != sizeof(date_time_form_t)) {
            return;
        }
        date_time_form_t* time_form = (date_time_form_t*)data;
        app_schedule_set_time(schedule_handle, *time_form);
        app_nextion_set_console(nextion_handle, "Thiết lập thời gian thành công");
        break;
    }
    case APP_NEXTION_EVENT_SYNC_TIME:
        ESP_LOGD(TAG, "Sync time");
        app_schedule_sync_time(schedule_handle);
        app_nextion_set_console(nextion_handle, "Đồng bộ thời gian thành công");
        break;
    case APP_NEXTION_EVENT_SET_SCHEDULE: {
        ESP_LOGD(TAG, "Set schedule");
        if (data == NULL || data_len != sizeof(active_time_t)) {
            return;
        }
        active_time_t* active_time = (active_time_t*)data;
        app_storage_data_t storage_data = {
            .start_hour = active_time->start_hour,
            .start_minute = active_time->start_minute,
            .enable_start = true,
            .end_hour = active_time->end_hour,
            .end_minute = active_time->end_minute,
            .enable_end = true
        };
        app_storage_set_data(storage_handle, &storage_data);
        app_storage_save_data(storage_handle);
        app_schedule_add_alarm(schedule_handle, storage_data.start_hour, storage_data.start_minute, SCHEDULE_TYPE_ACTIVE_START); 
        app_schedule_add_alarm(schedule_handle, storage_data.end_hour, storage_data.end_minute, SCHEDULE_TYPE_ACTIVE_END);
        app_websocket_update_active_time(websocket_handle, *active_time);
        app_nextion_set_console(nextion_handle, "Thiết lập thành công");
        break;
    }
    case APP_GPS_EVENT_SET_TIME:
        if (data == NULL || data_len != sizeof(date_time_form_t))
        {
            return;
        }
        date_time_form_t *time_form = (date_time_form_t *)data;
        // ESP_LOGI(TAG, "Gps convert main: %02d:%02d:%02d  %02d/%02d/%04d", time_form->hour,
        //          time_form->minute,
        //          time_form->day,
        //          time_form->month,
        //          time_form->year);
        app_gps_set_time(schedule_handle, *time_form);
        ESP_LOGD(TAG, "Set time gps");
        break;
    break;
    case APP_NEXTION_EVENT_MANUAL_ON: {
        ESP_LOGD(TAG, "Has a manual on");
        app_controller_control_relay(controller_handle, true);
        app_nextion_set_console(nextion_handle, "Điều khiển bật");
        break;
    }
    case APP_NEXTION_EVENT_MANUAL_OFF: {
        ESP_LOGD(TAG, "Has a manual off");
        app_controller_control_relay(controller_handle, false);
        app_nextion_set_console(nextion_handle, "Điều khiển tắt");
        break;
    }
    case APP_MODBUS_EVENT_DATA: {
        if (data == NULL || data_len != sizeof(phases_data_t)) {
            return;
        }
        phases_data_t* phases_data = (phases_data_t*)data;
        ESP_LOGE(TAG, "Phase data update: u1=%f, i1=%f p1=%f w1=%f", phases_data->u1, phases_data->i1, phases_data->p1, phases_data->phy1);
        ESP_LOGE(TAG, "Phase data update: u2=%f, i2=%f p2=%f w2=%f", phases_data->u2, phases_data->i2, phases_data->p2, phases_data->phy2);
        ESP_LOGE(TAG, "Phase data update: u3=%f, i3=%f p3=%f w3=%f", phases_data->u3, phases_data->i3, phases_data->p3, phases_data->phy3);
        ESP_LOGE(TAG, "Phase data update: Power=%f", phases_data->power);
        app_nextion_set_phase_data(nextion_handle, phases_data);
        app_websocket_update_meter_data(websocket_handle, phases_data);
        break;
    }
    case APP_CONTROLLER_EVENT_CHANGE_MODE: {
        app_controller_mode_t mode = (*(app_controller_mode_t*)data);
        ESP_LOGD(TAG, "has a input trigger %d", mode);
        app_nextion_set_mode(nextion_handle, mode);
        app_schedule_set_mode(schedule_handle, (operator_mode_t)mode);
        if (mode == APP_CONTROLLER_INPUT_MODE_OFF) {
            app_controller_control_relay(controller_handle, false);
        }
        app_websocket_update_mode(websocket_handle, mode);
        
        break;
    }
    case APP_CONTROLLER_EVENT_CHANGE_POWER: {
        if (data == NULL) {
            return;
        }
        bool* is_on = (bool*)data;
        ESP_LOGD(TAG, "change power event %d", *is_on ? 1 : 0);
        app_websocket_update_power_state(websocket_handle, *is_on);
        break;
    }
    case APP_SCHEDULE_EVENT_TRIGGER: {
        app_schedule_schedule_type_t* type = (app_schedule_schedule_type_t*)data;
        // ESP_LOGD(TAG, "Has a schedule trigger %d", *type);
        if (*type == SCHEDULE_TYPE_ACTIVE_START) {
            app_controller_control_relay(controller_handle, true);
        } else {
            app_controller_control_relay(controller_handle, false);
        }
        break;
    }
    default:
        break;
    }
}

void enable_log() {
    esp_log_level_set("WEBSOCKET_CLIENT", ESP_LOG_INFO);
    esp_log_level_set("TRANSPORT_WS", ESP_LOG_INFO);
    esp_log_level_set("RANDOM", ESP_LOG_INFO);
    esp_log_level_set("event", ESP_LOG_INFO);
}

void app_main(void)
{

    enable_log();

    char* hardware_id;

    uint64_t hw_id = 0;
    esp_efuse_mac_get_default((uint8_t *)&hw_id);
    hw_id >>= 24;

    asprintf(&hardware_id, "GW-%06llX", hw_id);

    ESP_LOGI(TAG, "HardwareId %s", hardware_id);

    storage_handle = app_storage_init();

    if (app_storage_load_data(storage_handle) != ESP_OK) {
        ESP_LOGI(TAG, "Load storage fail");
    } else {
        app_storage_print_data(storage_handle);
    }

    app_storage_data_t* storage_data = app_storage_get_data(storage_handle);

    ESP_LOGI(TAG, "[Firmware version] %s", FIRMWARE_VERSION);
  
    // gpio_reset_pin(PWR_SIM);
    // gpio_reset_pin(NET_SIM);
    // gpio_reset_pin(S0);
    // gpio_reset_pin(S1);
    // gpio_reset_pin(GPS_ACTIVE);
    
#if defined ENABLE_NETWORK
    app_network_config_t network_config = {
        .is_using_wifi = false,
    };

    network_handle = app_network_init(&network_config);
    app_network_start(network_handle);
#endif

#if (defined ENABLE_WEBSOCKET) && (defined ENABLE_NETWORK)
    app_websocket_config_t websocket_config = {
        .network_handle = network_handle,
        .callback = &event_callback,
        .websocket_url = "wss://api.lighting.sdkd.com.vn/websocket",
        // .websocket_url = "ws://192.168.0.103:8443/websocket",
        .init_active_time = {
            .start_hour = storage_data->start_hour,
            .end_hour = storage_data->end_hour,
            .start_minute = storage_data->start_minute,
            .end_minute = storage_data->end_minute,
        },
        .hardware_id = hardware_id,
    };

    websocket_handle = app_websocket_init(&websocket_config);
    app_websocket_start(websocket_handle);
#endif



#if (defined ENABLE_OTA) && (defined ENABLE_NETWORK)
    app_ota_cfg_t ota_config = {
        .network_handle = network_handle,
        // .host = "http://192.168.0.103:8443",
        .host = "https://api.lighting.sdkd.com.vn",
        .hardware_id = hardware_id,
        .version = FIRMWARE_VERSION,
        .update_interval_in_seconds = 3000,
    };

    ota_handle = app_ota_init(&ota_config);
#endif

#if defined ENABLE_MODBUS
    app_modbus_config_t modbus_config = {
        .baudrate = 9600,
        .port_num = 0,
        .tx_pin = 3,
        .rx_pin = 1,
        // .tx_pin = 5,
        // .rx_pin = 4,
        .update_interval_ms = 60000,
        .callback = &event_callback,
    };

    app_modbus_handle_t modbus_handle = app_modbus_init(&modbus_config);
    app_modbus_start(modbus_handle);
#endif

#if defined ENABLE_NEXTION
    app_nextion_config_t nextion_config = {
        // .tx_pin = 22,
        // .rx_pin = 23,
        .tx_pin = 4,
        .rx_pin = 5,
        .port_num = 2,
        .baudrate = 115200,
        .init_active_time = {
            .start_hour = storage_data->start_hour,
            .end_hour = storage_data->end_hour,
            .start_minute = storage_data->start_minute,
            .end_minute = storage_data->end_minute,
        },
        .callback = &event_callback,
        .hardware_id = hardware_id,
    };

    nextion_handle = app_nextion_init(&nextion_config);

    app_nextion_start(nextion_handle);
#endif

#if defined ENABLE_SCHEDULE

    app_schedule_config_t schedule_config = {
        .callback = &event_callback,
        .network_handle = network_handle,
        .scl_pin = 22,
        .sda_pin = 21,
        .risk_offset = 5,
    };

    schedule_handle = app_schedule_init(&schedule_config);

    app_schedule_start(schedule_handle);

    if (storage_data->enable_start) {
        app_schedule_add_alarm(schedule_handle, storage_data->start_hour, storage_data->start_minute, SCHEDULE_TYPE_ACTIVE_START); 
    }

    if (storage_data->enable_end) {
        app_schedule_add_alarm(schedule_handle, storage_data->end_hour, storage_data->end_minute, SCHEDULE_TYPE_ACTIVE_END); 
    }

    // app_schedule_add_alarm(schedule_handle, 0, 40, SCHEDULE_TYPE_ACTIVE_END);

#endif


#if defined ENABLE_GPS
    app_gps_config_t app_gps_config = {
        // .uart_config.baud_rate = 9600,
        // .uart_config.data_bits = UART_DATA_8_BITS,
        // .uart_config.parity = UART_PARITY_DISABLE,
        // .uart_config.stop_bits = UART_STOP_BITS_1,
        // .uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        // .uart_config.source_clk = UART_SCLK_APB,
    //         int uartPort; // Cau hinh port 1
    // int tx_pin;
    // int rx_pin;
        .tx_pin = 17,
        .rx_pin = 16,
        .callback = &event_callback,
        .network_handle = network_handle,
        .schedule = schedule_handle,
    };
    gps_handle = app_gps_init(&app_gps_config);
    app_gps_start(gps_handle);
#endif

    app_controller_config_t controller_config = {
        .callback = &event_callback,
        .relay_1_pin = 25,
        .relay_2_pin = 26,
        .switcher_auto_pin = 19,
        .switcher_manual_pin = 18,
        .status_pin = 27
    };

    controller_handle = app_controller_init(&controller_config);

    int page = 0;

// */
    while (1) {
        // app_nextion_set_page(nextion_handle, page);
        page++;
        page %= 8;
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }

}
