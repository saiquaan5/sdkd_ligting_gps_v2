#ifndef _APP_NEXTION_H_
#define _APP_NEXTION_H_

#include "app_common.h"
#include "app_controller.h"

#define PAGE_LOCK_INDEX     0
#define PAGE_HOME_INDEX     1
#define PAGE_MANUAL_INDEX   2
#define PAGE_SETTING_INDEX  3
#define PAGE_TIME_INDEX     4
#define PAGE_SCHEDULE_INDEX 5
#define PAGE_CONNECT_INDEX  6
#define PAGE_INFO_INDEX     7
#define PAGE_LOADING_INDEX  9

typedef enum {
    APP_NEXTION_EVENT_MANUAL_ON = APP_NEXTION_EVENT_BASE + 1,
    APP_NEXTION_EVENT_MANUAL_OFF = APP_NEXTION_EVENT_BASE + 2,
    APP_NEXTION_EVENT_SYNC_TIME = APP_NEXTION_EVENT_BASE + 3,
    APP_NEXTION_EVENT_SET_TIME = APP_NEXTION_EVENT_BASE + 4,
    APP_NEXTION_EVENT_SET_SCHEDULE = APP_NEXTION_EVENT_BASE + 5,
} app_nextion_event_id_t;

typedef struct {
    int tx_pin;
    int rx_pin;
    int baudrate;
    int port_num;
    app_callback_t* callback;
    active_time_t init_active_time;
    char* hardware_id;
} app_nextion_config_t;

typedef struct app_nextion_* app_nextion_handle_t;

app_nextion_handle_t app_nextion_init(app_nextion_config_t* config);

esp_err_t app_nextion_start(app_nextion_handle_t handle);

void app_nextion_set_page(app_nextion_handle_t handle, int page_num);

void app_nextion_set_console(app_nextion_handle_t handle, const char* message);

void app_nextion_set_button(app_nextion_handle_t handle, bool is_on);

esp_err_t app_nextion_set_conection_state(app_nextion_handle_t handle, bool is_connected);

esp_err_t app_nextion_set_power_state(app_nextion_handle_t handle, bool is_on);

esp_err_t app_nextion_set_phase_data(app_nextion_handle_t handle, phases_data_t* phase_data);

esp_err_t app_nextion_set_active_time(app_nextion_handle_t handle, active_time_t* active_time);

esp_err_t app_nextion_set_mode(app_nextion_handle_t handle, app_controller_mode_t mode);

esp_err_t app_nextion_set_loading(app_nextion_handle_t handle, int percent, char* console);

#endif