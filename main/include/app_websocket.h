#ifndef _APP_WEBSOCKET_H_
#define _APP_WEBSOCKET_H_

#include "app_common.h"
#include "app_network.h"

typedef enum {
    WEBSOCKET_ON_CONNECTED = APP_WEBSOCKET_EVENT_BASE + 1,
    WEBSOCKET_ON_DISCONNECTED = APP_WEBSOCKET_EVENT_BASE + 2,
    WEBSOCKET_ON_CONTROL = APP_WEBSOCKET_EVENT_BASE + 3,
    WEBSOCKET_ON_ONLINE = APP_WEBSOCKET_EVENT_BASE + 4,
    WEBSOCKET_ON_SETTING_ACTIVE_TIME = APP_WEBSOCKET_EVENT_BASE + 5,
    WEBSOCKET_ON_FORCE_OTA = APP_WEBSOCKET_EVENT_BASE + 6,
    WEBSOCKET_ON_FREE = APP_WEBSOCKET_EVENT_BASE + 7,
} app_websocket_event_id_t;

typedef struct {
    bool is_on;
    operator_mode_t mode;
} app_websocket_control_form_t;

typedef struct {
    app_network_handle_t network_handle;
    app_callback_t* callback;
    const char* websocket_url;
    active_time_t init_active_time;
    char* hardware_id;
} app_websocket_config_t;

typedef struct app_websocket_* app_websocket_handle_t;

app_websocket_handle_t app_websocket_init(app_websocket_config_t* config);

void app_websocket_start(app_websocket_handle_t handle);

void app_websocket_send(app_websocket_handle_t handle, uint8_t* data, int len);

esp_err_t app_websocket_update_power_state(app_websocket_handle_t handle, bool is_on);

esp_err_t app_websocket_update_active_time(app_websocket_handle_t handle, active_time_t active_time);

esp_err_t app_websocket_update_mode(app_websocket_handle_t handle, int mode);

esp_err_t app_websocket_update_meter_data(app_websocket_handle_t handle, phases_data_t* phases_data);

#endif