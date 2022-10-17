#ifndef _APP_SCHEDULE_H_
#define _APP_SCHEDULE_H_

#include "app_common.h"
#include "app_network.h"

typedef enum {
    APP_SCHEDULE_EVENT_TRIGGER = APP_SCHEDULE_EVENT_BASE + 1,
} app_schedule_event_id_t;

typedef enum {
    SCHEDULE_TYPE_NONE = 0,
    SCHEDULE_TYPE_ACTIVE_START,
    SCHEDULE_TYPE_ACTIVE_END,
    SCHEDULE_TYPE_MAX,
} app_schedule_schedule_type_t;

typedef struct {
    app_network_handle_t network_handle;
    app_callback_t* callback;
    int sda_pin;
    int scl_pin;
    int risk_offset;
} app_schedule_config_t;

typedef struct app_schedule_* app_schedule_handle_t;

app_schedule_handle_t app_schedule_init(app_schedule_config_t* config);

esp_err_t app_schedule_start(app_schedule_handle_t handle);

esp_err_t app_schedule_add_alarm(app_schedule_handle_t handle, int hour, int minute, app_schedule_schedule_type_t type);

esp_err_t app_schedule_set_time(app_schedule_handle_t handle, date_time_form_t time_form);

esp_err_t app_schedule_sync_time(app_schedule_handle_t handle);

esp_err_t app_schedule_set_mode(app_schedule_handle_t handle, operator_mode_t mode);

#endif