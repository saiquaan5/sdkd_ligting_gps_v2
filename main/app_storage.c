#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "app_storage.h"

#define TAG     "APP_STORAGE"

#define STORAGE_NAMESPACE   "app"
#define DATA_KEY            "COMMON_KEY"

typedef struct app_storage_ {
    app_storage_data_t* data;
} app_storage_t;

app_storage_handle_t app_storage_init() {
    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGD(TAG, "first init");
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        if (nvs_flash_erase() != ESP_OK) {
            return NULL;
        }
        err = nvs_flash_init();
        if (err != ESP_OK) {
            return NULL;
        }
    }
    app_storage_handle_t handle = malloc(sizeof(app_storage_t));
    handle->data = malloc(sizeof(app_storage_data_t));
    return handle;
}

esp_err_t app_storage_load_data(app_storage_handle_t handle) {
    if (handle == NULL) {
        return ESP_FAIL;
    }
    nvs_handle_t my_handle;
    esp_err_t err;

    // Open
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "open nvs failed");
        return err;
    }

    // Read run time blob
    size_t required_size = 0;  // value will default to 0, if not set yet in NVS
    // obtain required memory space to store blob being read from NVS

    memset(handle->data, 0, sizeof(app_storage_data_t));
    handle->data->enable_start = false;
    handle->data->enable_end = false;

    err = nvs_get_blob(my_handle, DATA_KEY, NULL, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    ESP_LOGD(TAG, "Run time:\n");
    if (required_size == 0 || required_size != sizeof(app_storage_data_t)) {
        ESP_LOGE(TAG, "invalid storage data");
        nvs_set_blob(my_handle, DATA_KEY, handle->data, sizeof(app_storage_data_t));
    } else {
        err = nvs_get_blob(my_handle, DATA_KEY, handle->data, &required_size);
        if (err != ESP_OK) {
            return err;
        }
        ESP_LOGI(TAG, "get data success");
    }

    // Close
    nvs_close(my_handle);
    return ESP_OK;
}

app_storage_data_t* app_storage_get_data(app_storage_handle_t handle) {
    return handle->data;
}

esp_err_t app_storage_set_data(app_storage_handle_t handle, app_storage_data_t* data) {
    if (handle == NULL) {
        return ESP_FAIL;
    }
    memcpy(handle->data, data, sizeof(app_storage_data_t));
    return ESP_OK;
}

esp_err_t app_storage_save_data(app_storage_handle_t handle) {
    if (handle == NULL) {
        return ESP_FAIL;
    }
    nvs_handle_t my_handle;
    esp_err_t err;

    // Open
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "open nvs failed");
        return err;
    }

    // Read run time blob
    nvs_set_blob(my_handle, DATA_KEY, handle->data, sizeof(app_storage_data_t));
    // Close
    nvs_close(my_handle);
    return ESP_OK;
}

void app_storage_print_data(app_storage_handle_t handle) {
    if (handle == NULL) {
        return;
    }
    ESP_LOGI(TAG, "active time: %02d:%02d - %02d:%02d, enable: start[%d] - end[%d]",
        handle->data->start_hour,
        handle->data->start_minute,
        handle->data->end_hour,
        handle->data->end_minute,
        (int)handle->data->enable_start,
        (int)handle->data->enable_end
    );
}