#ifndef _APP_OTA_H_
#define _APP_OTA_H_

#include "esp_err.h"
#include "app_network.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *host;
    const char *version;
    const char *update_template;
    const char *token;
    const char *model;
    int update_interval_in_seconds;
    bool disable_auto_ota;
    app_network_handle_t network_handle;
    char *hardware_id;
} app_ota_cfg_t;

typedef enum  {
    OTA_STATUS_UNKNOWN,
    OTA_STATUS_UPDATE_PREPARE,
    OTA_STATUS_UPDATE_BEGIN,
    OTA_STATUS_UPDATE_PROCESS,
    OTA_STATUS_UPDATE_FINISH,
    OTA_STATUS_UPDATE_EXIT,
    OTA_STATUS_UPDATE_ERROR,
    OTA_STATUS_INVALID_TOKEN,
} app_ota_event_id_t;

typedef struct app_ota_* app_ota_handle_t;

app_ota_handle_t app_ota_init(app_ota_cfg_t *ota_cfg);

esp_err_t app_ota_force_update(app_ota_handle_t periph);

#ifdef __cplusplus
}
#endif

#endif
