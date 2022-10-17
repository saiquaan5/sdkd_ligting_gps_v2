/*
 * This file is subject to the terms of the Nanochip License. If a copy of
 * the license was not distributed with this file, you can obtain one at:
 *                             ./LICENSE
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/sens_reg.h"

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "app_ota.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "freertos/semphr.h"

static const char* TAG = "APP_OTA";

#define OTA_BUFFSIZE 1024
#define OTA_CHECK_DELAY_SEC (10)
#define OTA_NETWORK_TIMEOUT_MS (30*1000)
#define DEFAULT_OTA_TASK_STACK (5*1024)

#define URL_TEMPLATE    "%s/api/v1/ota/check?firmware_version=%s&hardware_id=%s"

typedef struct app_ota_ {
    char *host;
    char *update_template;
    char *token;
    char *version;
    char *hardware_id;
    char *model;
    int update_interval_in_seconds;
    int tick;
    bool updating;
    bool disable_auto_ota;
    bool disable_acquire_connection;
    app_network_handle_t network_handle;
    SemaphoreHandle_t force_ota_flag;
} app_ota_t;

typedef enum  {
    OTA_CMD_UNKNOWN,
    OTA_CMD_UPDATE,
} app_ota_cmd_id_t;

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    }
    return ESP_OK;
}

void _ota_task(void *pv)
{
    app_ota_handle_t handle = (app_ota_handle_t)pv;

    char* url;

    asprintf(&url, URL_TEMPLATE, handle->host, handle->version, handle->hardware_id);

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _http_event_handler,
        .cert_pem = NULL,
    };

    while (1) {

        xSemaphoreTake(handle->force_ota_flag, portMAX_DELAY);

        if (!app_network_is_connected(handle->network_handle)) {
            ESP_LOGW(TAG, "network error");
            continue;
        }

        ESP_LOGI(TAG, "Attempting to download update from %s", config.url);
        esp_err_t ret = esp_https_ota(&config);
        if (ret == ESP_OK) {
            esp_restart();
        } else {
            ESP_LOGW(TAG, "There are no new firmware version");
        }

    }

    vTaskDelete(NULL);
}

app_ota_handle_t app_ota_init(app_ota_cfg_t *ota_cfg)
{

    if (ota_cfg->version == NULL || ota_cfg->host == NULL) {
        ESP_LOGE(TAG, "Invalid argument");
        return NULL;
    }

    app_ota_handle_t handle = malloc(sizeof(app_ota_t));

    handle->update_interval_in_seconds = ota_cfg->update_interval_in_seconds;

    handle->network_handle = ota_cfg->network_handle;

    handle->host = ota_cfg->host ? strdup(ota_cfg->host) : strdup("");
    handle->hardware_id = ota_cfg->hardware_id ? strdup(ota_cfg->hardware_id) : strdup("");

    if (ota_cfg->model) {
        handle->model = strdup(ota_cfg->model);
    } else {
        handle->model = strdup("");
    }
    if (ota_cfg->version == NULL) {
        goto _ota_init_fail;
    }
    handle->version = strdup(ota_cfg->version);

    handle->force_ota_flag = xSemaphoreCreateBinary();

    if (xTaskCreate(_ota_task, "ota_task", 4 * 1024, (void *)handle, 5, NULL) != pdTRUE) {
        ESP_LOGE(TAG, "Error create ota_task");
        return ESP_OK;
    }

    return handle;
_ota_init_fail:
    ESP_LOGE(TAG, "Failed to initial OTA");
    free(handle->update_template);
    free(handle->token);
    free(handle->version);
    free(handle->model);
    free(handle);
    return NULL;
}

esp_err_t app_ota_force_update(app_ota_handle_t handle)
{
    if (handle == NULL) {
        return ESP_FAIL;
    }
    xSemaphoreGive(handle->force_ota_flag);
    return ESP_OK;
}
