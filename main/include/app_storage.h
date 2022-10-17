#ifndef _APP_STORAGE_H_
#define _APP_STORAGE_H_

#include "app_common.h"

typedef struct {
    int start_hour;
    int start_minute;
    bool enable_start;
    int end_hour;
    int end_minute;
    bool enable_end;
} app_storage_data_t;

typedef struct app_storage_* app_storage_handle_t;

app_storage_handle_t app_storage_init();

esp_err_t app_storage_load_data(app_storage_handle_t handle);

app_storage_data_t* app_storage_get_data(app_storage_handle_t handle);

esp_err_t app_storage_set_data(app_storage_handle_t handle, app_storage_data_t* data);

esp_err_t app_storage_save_data(app_storage_handle_t handle);

void app_storage_print_data(app_storage_handle_t handle);

#endif