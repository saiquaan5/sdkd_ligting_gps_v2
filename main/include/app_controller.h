#ifndef _APP_CONTROLLER_H_
#define _APP_CONTROLLER_H_

#include "app_common.h"

typedef enum {
    APP_CONTROLLER_EVENT_CHANGE_MODE = APP_CONTROLLER_EVENT_BASE + 1,
    APP_CONTROLLER_EVENT_CHANGE_POWER = APP_CONTROLLER_EVENT_BASE + 2,
} app_controller_event_id_t;

typedef enum {
    APP_CONTROLLER_INPUT_MODE_OFF = 0,
    APP_CONTROLLER_INPUT_MODE_AUTO = 1,
    APP_CONTROLLER_INPUT_MODE_MANUAL = 2,
} app_controller_mode_t;

typedef struct {
    int relay_1_pin;
    int relay_2_pin;
    int switcher_auto_pin;
    int switcher_manual_pin;
    app_callback_t* callback;
    int status_pin;
} app_controller_config_t;

typedef struct app_controller_* app_controller_handle_t;

app_controller_handle_t app_controller_init(app_controller_config_t* config);

esp_err_t app_controller_control_relay(app_controller_handle_t handle, bool is_active);

bool app_controller_get_switcher_state(app_controller_handle_t handle);

esp_err_t app_controller_force_manual(app_controller_handle_t handle, bool is_release);

app_controller_mode_t app_controller_get_mode(app_controller_handle_t handle);

#endif