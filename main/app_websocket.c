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
#include "esp_log.h"
#include "driver/gpio.h"
#include "app_websocket.h"
#include "app_network.h"
#include "esp_websocket_client.h"
#include "esp_event.h"
#include "freertos/ringbuf.h"
#include "app_controller.h"

#include "common_proto.pb-c.h"
#include "device_proto.pb-c.h"
#include "main_proto.pb-c.h"

#define DEFAULT_MESH_RINGBUFFER_SIZE (4*1024)

#define REGISTER_TIMEOUT_MS (30*1000)
#define LOGIN_TIMEOUT_MS    (30*1000)
#define PING_TIMEOUT_MS     (30*1000)

#define WS_BUFFER_SIZE (1024 * 5)

static const char* TAG = "APP_WEBSOCKET";

typedef struct app_websocket_ {
    bool running;
    app_network_handle_t network_handle;
    app_callback_t* callback;
    bool websocket_connected;
    char* websocket_url;
    esp_websocket_client_handle_t client;
    bool is_start_websocket;
    uint8_t* ws_tx_buffer;
    RingbufHandle_t rx_rb;
    RingbufHandle_t tx_rb;
    char* device_id;
    char* hardware_id;
    char* firmware_version;
    bool is_logged;
    long last_reg_tick;
    long last_login_tick;
    long last_ping_tick;
    bool power_state;
    active_time_t active_time;
    app_controller_mode_t mode;
} app_websocket_t;


app_websocket_handle_t app_websocket_init(app_websocket_config_t* config) {
    app_websocket_handle_t handle = malloc(sizeof(app_websocket_t));
    handle->network_handle = config->network_handle;
    handle->callback = config->callback;
    handle->running = false;
    handle->websocket_connected = false;
    handle->websocket_url = strdup(config->websocket_url);
    handle->is_start_websocket = false;
    handle->ws_tx_buffer = malloc(WS_BUFFER_SIZE);
    handle->rx_rb = xRingbufferCreate(DEFAULT_MESH_RINGBUFFER_SIZE, RINGBUF_TYPE_NOSPLIT);
    handle->tx_rb = xRingbufferCreate(DEFAULT_MESH_RINGBUFFER_SIZE, RINGBUF_TYPE_NOSPLIT);
    handle->device_id = NULL;
    handle->firmware_version = strdup(FIRMWARE_VERSION);
    handle->last_login_tick = -LOGIN_TIMEOUT_MS;
    handle->last_ping_tick = -PING_TIMEOUT_MS;
    handle->last_reg_tick = -REGISTER_TIMEOUT_MS;
    handle->power_state = 0;
    handle->mode = 0;
    memcpy(&handle->active_time, &config->init_active_time, sizeof(active_time_t));

    handle->hardware_id = strdup(config->hardware_id);

    return handle;
}

static void _callback(app_websocket_handle_t handle, int event_id, void* data, int data_len) {
    if (handle->callback != NULL) {
        handle->callback(event_id, data, data_len);
    }
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    app_websocket_handle_t handle = (app_websocket_handle_t)handler_args;

    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED");
            handle->websocket_connected = true;
            handle->is_logged = false;
            _callback(handle, WEBSOCKET_EVENT_CONNECTED, NULL, 0);
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "WEBSOCKET_EVENT_DISCONNECTED");
            handle->websocket_connected = false;
            handle->is_logged = false;
            _callback(handle, WEBSOCKET_ON_DISCONNECTED, NULL, 0);
            break;
        case WEBSOCKET_EVENT_DATA:
            if (data->data_len <= 0) {
                break;
            }
            ESP_LOGI(TAG, "WEBSOCKET_EVENT_DATA");
            ESP_LOGI(TAG, "Received opcode=%d", data->op_code);
            ESP_LOGW(TAG, "Received=%.*s", data->data_len, (char *)data->data_ptr);
            ESP_LOGW(TAG, "Total payload length=%d, data_len=%d, current payload offset=%d\r\n", data->payload_len, data->data_len, data->payload_offset);
            if (xRingbufferSend(handle->rx_rb, data->data_ptr, data->data_len, 10000 / portTICK_RATE_MS) != pdTRUE) {
                ESP_LOGE(TAG, "Error enqueuing...");
            }
            break;
        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGI(TAG, "WEBSOCKET_EVENT_ERROR");
            break;
    }
}

void _update_power_state(app_websocket_handle_t handle, bool is_on) {
    if (handle == NULL) {
        return;
    }
    ESP_LOGI(TAG, "Update power state %d", is_on ? 1 : 0);
    MainMessage mainMessage = MAIN_MESSAGE__INIT;
    DeviceMessage deviceMessage = DEVICE_MESSAGE__INIT;
    DeviceUpdateMessage deviceUpdateMessage = DEVICE_UPDATE_MESSAGE__INIT;
    UpdatePowerStateDeviceRequest updatePowerStateDeviceRequest = UPDATE_POWER_STATE_DEVICE_REQUEST__INIT;
    
    updatePowerStateDeviceRequest.deviceid = handle->device_id;
    updatePowerStateDeviceRequest.ison = is_on > 0 ? true : false;

    deviceUpdateMessage.updatepowerstatedevicerequest = &updatePowerStateDeviceRequest;
    deviceMessage.deviceupdatemessage = &deviceUpdateMessage;
    mainMessage.devicemessage = &deviceMessage;

    int size = main_message__pack(&mainMessage, handle->ws_tx_buffer);

    if (xRingbufferSend(handle->tx_rb, handle->ws_tx_buffer, size, 2000 / portTICK_RATE_MS) != pdTRUE) {
        ESP_LOGE(TAG, "Error enqueuing...");
    }
}

void _update_mode(app_websocket_handle_t handle, int mode) {
    if (handle == NULL) {
        return;
    }
    ESP_LOGI(TAG, "Update mode %d", mode);
    MainMessage mainMessage = MAIN_MESSAGE__INIT;
    DeviceMessage deviceMessage = DEVICE_MESSAGE__INIT;
    DeviceUpdateMessage deviceUpdateMessage = DEVICE_UPDATE_MESSAGE__INIT;
    UpdateModeDeviceRequest updateModeDeviceRequest = UPDATE_MODE_DEVICE_REQUEST__INIT;
    
    updateModeDeviceRequest.deviceid = handle->device_id;
    updateModeDeviceRequest.mode = mode;

    deviceUpdateMessage.updatemodedevicerequest = &updateModeDeviceRequest;
    deviceMessage.deviceupdatemessage = &deviceUpdateMessage;
    mainMessage.devicemessage = &deviceMessage;

    int size = main_message__pack(&mainMessage, handle->ws_tx_buffer);

    if (xRingbufferSend(handle->tx_rb, handle->ws_tx_buffer, size, 2000 / portTICK_RATE_MS) != pdTRUE) {
        ESP_LOGE(TAG, "Error enqueuing...");
    }
}

void _update_meter_data(app_websocket_handle_t handle, phases_data_t* phase_data) {
    if (handle == NULL) {
        return;
    }
    ESP_LOGI(TAG, "Update phase data");
    MainMessage mainMessage = MAIN_MESSAGE__INIT;
    DeviceMessage deviceMessage = DEVICE_MESSAGE__INIT;
    DeviceUpdateMessage deviceUpdateMessage = DEVICE_UPDATE_MESSAGE__INIT;
    UpdateMeterDataDeviceRequest updateMeterDataReceiveRequest = UPDATE_METER_DATA_DEVICE_REQUEST__INIT;
    
    updateMeterDataReceiveRequest.deviceid = handle->device_id;
    updateMeterDataReceiveRequest.i1 = phase_data->i1;
    updateMeterDataReceiveRequest.i2 = phase_data->i2;
    updateMeterDataReceiveRequest.i3 = phase_data->i3;

    updateMeterDataReceiveRequest.a1 = phase_data->phy1;
    updateMeterDataReceiveRequest.a2 = phase_data->phy2;
    updateMeterDataReceiveRequest.a3 = phase_data->phy3;

    updateMeterDataReceiveRequest.v1 = phase_data->u1;
    updateMeterDataReceiveRequest.v2 = phase_data->u2;
    updateMeterDataReceiveRequest.v3 = phase_data->u3;

    updateMeterDataReceiveRequest.p1 = phase_data->p1;
    updateMeterDataReceiveRequest.p2 = phase_data->p2;
    updateMeterDataReceiveRequest.p3 = phase_data->p3;

    updateMeterDataReceiveRequest.power = phase_data->power;

    deviceUpdateMessage.updatemeterdatadevicerequest = &updateMeterDataReceiveRequest;
    deviceMessage.deviceupdatemessage = &deviceUpdateMessage;
    mainMessage.devicemessage = &deviceMessage;

    int size = main_message__pack(&mainMessage, handle->ws_tx_buffer);

    if (xRingbufferSend(handle->tx_rb, handle->ws_tx_buffer, size, 2000 / portTICK_RATE_MS) != pdTRUE) {
        ESP_LOGE(TAG, "Error enqueuing...");
    }
}

void _update_active_time(app_websocket_handle_t handle, active_time_t active_time) {
    if (handle == NULL) {
        return;
    }
    ESP_LOGI(TAG, "Update active time");
    MainMessage mainMessage = MAIN_MESSAGE__INIT;
    DeviceMessage deviceMessage = DEVICE_MESSAGE__INIT;
    DeviceUpdateMessage deviceUpdateMessage = DEVICE_UPDATE_MESSAGE__INIT;
    DeviceUpdateScheduleRequest deviceUpdateScheduleRequest = DEVICE_UPDATE_SCHEDULE_REQUEST__INIT;

    ActiveLamp activeLamp = ACTIVE_LAMP__INIT;

    HMTime start = HMTIME__INIT;
    HMTime end = HMTIME__INIT;

    start.hour = active_time.start_hour;
    start.minute = active_time.start_minute;
    end.hour = active_time.end_hour;
    end.minute = active_time.end_minute;

    activeLamp.begin = &start;
    activeLamp.end = &end;

    deviceUpdateScheduleRequest.deviceid = handle->device_id;
    deviceUpdateScheduleRequest.active_time = &activeLamp;
    
    deviceUpdateMessage.deviceupdateschedulerequest = &deviceUpdateScheduleRequest;
    deviceMessage.deviceupdatemessage = &deviceUpdateMessage;
    mainMessage.devicemessage = &deviceMessage;

    int size = main_message__pack(&mainMessage, handle->ws_tx_buffer);

    if (xRingbufferSend(handle->tx_rb, handle->ws_tx_buffer, size, 2000 / portTICK_RATE_MS) != pdTRUE) {
        ESP_LOGE(TAG, "Error enqueuing...");
    }
}

esp_err_t _app_ws_prepare(app_websocket_handle_t handle) {
    if (!app_network_is_connected(handle->network_handle)) {
        ESP_LOGD(TAG, "Waiting for network...");
        vTaskDelay(500 / portTICK_RATE_MS);
        return ESP_FAIL;
    }

    if (handle->websocket_connected) {
        return ESP_OK;
    }

    if (handle->is_start_websocket) {
        return ESP_FAIL;
    }

    esp_websocket_client_config_t websocket_cfg = {};

    websocket_cfg.uri = handle->websocket_url;

    ESP_LOGI(TAG, "Connecting to %s...", websocket_cfg.uri);

    handle->client = esp_websocket_client_init(&websocket_cfg);
    esp_websocket_register_events(handle->client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)handle);

    esp_websocket_client_start(handle->client);

    handle->is_start_websocket = true;

    return ESP_FAIL;   
}

esp_err_t _app_ws_register(app_websocket_handle_t handle) {
    if (handle->device_id != NULL && strlen(handle->device_id) > 0) {
        return ESP_OK;
    }

    if (esp_tick_get() < handle->last_reg_tick + REGISTER_TIMEOUT_MS) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
        return ESP_FAIL;
    }
    handle->last_reg_tick = esp_tick_get();

    ESP_LOGD(TAG, "To be register");

    MainMessage mainMessage = MAIN_MESSAGE__INIT;
    DeviceMessage deviceMessage = DEVICE_MESSAGE__INIT;
    DeviceAuthMessage deviceAuthMessage = DEVICE_AUTH_MESSAGE__INIT;
    DeviceRegisterRequest deviceRegisterRequest = DEVICE_REGISTER_REQUEST__INIT;
    
    deviceRegisterRequest.hardwareid = handle->hardware_id;
    deviceRegisterRequest.firmwareversion = handle->firmware_version;
    deviceRegisterRequest.cpuimei = "112233445566";
    deviceRegisterRequest.mantoken = "112233445566";
    deviceRegisterRequest.numphase = 2;

    deviceAuthMessage.deviceregisterrequest = &deviceRegisterRequest;
    deviceMessage.deviceauthmessage = &deviceAuthMessage;
    mainMessage.devicemessage = &deviceMessage;

    int size = main_message__pack(&mainMessage, handle->ws_tx_buffer);

    app_websocket_send(handle, handle->ws_tx_buffer, size);

    return ESP_FAIL;
}

esp_err_t _app_ws_login(app_websocket_handle_t handle) {
    if (handle->is_logged) {
        return ESP_OK;
    }

    if (esp_tick_get() < handle->last_login_tick + LOGIN_TIMEOUT_MS) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
        return ESP_FAIL;
    }
    handle->last_login_tick = esp_tick_get();

    ESP_LOGD(TAG, "To be login");

    MainMessage mainMessage = MAIN_MESSAGE__INIT;
    DeviceMessage deviceMessage = DEVICE_MESSAGE__INIT;
    DeviceAuthMessage deviceAuthMessage = DEVICE_AUTH_MESSAGE__INIT;
    DeviceLoginRequest deviceLoginRequest = DEVICE_LOGIN_REQUEST__INIT;
    
    deviceLoginRequest.deviceid = handle->device_id;
    deviceLoginRequest.cpuimage = "112233445566";

    deviceAuthMessage.deviceloginrequest = &deviceLoginRequest;
    deviceMessage.deviceauthmessage = &deviceAuthMessage;
    mainMessage.devicemessage = &deviceMessage;

    int size = main_message__pack(&mainMessage, handle->ws_tx_buffer);

    app_websocket_send(handle, handle->ws_tx_buffer, size);

    return ESP_FAIL;
}

esp_err_t _app_ws_send_ping(app_websocket_handle_t handle) {

    if (esp_tick_get() < handle->last_ping_tick + PING_TIMEOUT_MS) {
        // vTaskDelay(10 / portTICK_PERIOD_MS);
        return ESP_OK;
    }
    handle->last_ping_tick = esp_tick_get();

    ESP_LOGD(TAG, "To be ping");

    MainMessage mainMessage = MAIN_MESSAGE__INIT;
    DeviceMessage deviceMessage = DEVICE_MESSAGE__INIT;
    DeviceUpdateMessage deviceUpdateMessage = DEVICE_UPDATE_MESSAGE__INIT;
    PingWebsocketDeviceRequest pingWebsocketDeviceRequest = PING_WEBSOCKET_DEVICE_REQUEST__INIT;
    
    pingWebsocketDeviceRequest.deviceid = handle->device_id;

    deviceUpdateMessage.pingwebsocketdevicerequest = &pingWebsocketDeviceRequest;
    deviceMessage.deviceupdatemessage = &deviceUpdateMessage;
    mainMessage.devicemessage = &deviceMessage;

    int size = main_message__pack(&mainMessage, handle->ws_tx_buffer);

    app_websocket_send(handle, handle->ws_tx_buffer, size);

    return ESP_FAIL;
}

esp_err_t _app_ws_sending_data(app_websocket_handle_t handle) {
    size_t item_sz;

    void *item = xRingbufferReceive(handle->tx_rb, &item_sz, 100 / portTICK_RATE_MS);

    if (item != NULL) {
        app_websocket_send(handle, item, item_sz);
        vRingbufferReturnItem(handle->tx_rb, item);
    }
    return ESP_OK;
}

void _websocket_task(void* pv) {
    app_websocket_handle_t handle = (app_websocket_handle_t)pv;

    while (1) {
        AWAIT(_app_ws_prepare(handle));
        AWAIT(_app_ws_register(handle));
        AWAIT(_app_ws_login(handle));
        AWAIT(_app_ws_sending_data(handle));
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

esp_err_t _process_data(app_websocket_handle_t handle, void* data, size_t sz) {

    ESP_LOGI(TAG, "processing %d bytes", sz);

    MainMessage *mainMessage = main_message__unpack(NULL, (size_t)sz, (uint8_t *) data);

    if (mainMessage == NULL) {
        ESP_LOGE(TAG, "parsed failed");
        return ESP_FAIL;
    }
    if (mainMessage->devicemessage &&
        mainMessage->devicemessage->deviceauthmessage &&
        mainMessage->devicemessage->deviceauthmessage->deviceregisterresponse) {
        DeviceRegisterResponse* deviceRegisterResponse = mainMessage->devicemessage->deviceauthmessage->deviceregisterresponse;
        ESP_LOGI(TAG, "Has Register response with code = %d and message = %s", deviceRegisterResponse->statuscode->code, deviceRegisterResponse->statuscode->message);
        if (deviceRegisterResponse->statuscode->code == 0) {
            ESP_LOGI(TAG, "Device Register success, device id = %s", deviceRegisterResponse->deviceid);
            if (deviceRegisterResponse != NULL) {
                if (handle->device_id != NULL) {
                    free(handle->device_id);
                }
                handle->device_id = strdup(deviceRegisterResponse->deviceid);
            }
        }
    } else if (mainMessage->devicemessage &&
        mainMessage->devicemessage->deviceauthmessage &&
        mainMessage->devicemessage->deviceauthmessage->deviceloginresponse) {
        DeviceLoginResponse* deviceLoginResponse = mainMessage->devicemessage->deviceauthmessage->deviceloginresponse;
        ESP_LOGI(TAG, "Has login response with code = %d and message = %s", deviceLoginResponse->statuscode->code, deviceLoginResponse->statuscode->message);
        if (deviceLoginResponse->statuscode->code == 0) {
            ESP_LOGI(TAG, "Device login success !");
            handle->is_logged = true;
            _callback(handle, WEBSOCKET_ON_ONLINE, NULL, 0);
            _update_power_state(handle, handle->power_state);
            _update_mode(handle, handle->mode);
            _update_active_time(handle, handle->active_time);
        }
    } else if (mainMessage->devicemessage &&
        mainMessage->devicemessage->userdevicemessage &&
        mainMessage->devicemessage->userdevicemessage->powercontroluserrequest) {
        PowerControlUserRequest* request = mainMessage->devicemessage->userdevicemessage->powercontroluserrequest;
        ESP_LOGI(TAG, "Has a power control request %d", (int)request->ispoweron);
        _callback(handle, WEBSOCKET_ON_CONTROL, &request->ispoweron, sizeof(bool));
    } else if (mainMessage->devicemessage &&
        mainMessage->devicemessage->userdevicemessage &&
        mainMessage->devicemessage->userdevicemessage->requirestreamdevicerequest) {
        ESP_LOGI(TAG, "Has a require stream request");
    } else if (mainMessage->devicemessage &&
        mainMessage->devicemessage->userdevicemessage &&
        mainMessage->devicemessage->userdevicemessage->controllanedevicerequest) {
        ControlLaneDeviceRequest* request = mainMessage->devicemessage->userdevicemessage->controllanedevicerequest;
        ESP_LOGI(TAG, "has control lane request is on %d %d ", request->ispoweron, request->laneindex);
    } else if (mainMessage->devicemessage &&
        mainMessage->devicemessage->userdevicemessage &&
        mainMessage->devicemessage->userdevicemessage->setactivetimedevicerequest) {
        SetActiveTimeDeviceRequest* request = mainMessage->devicemessage->userdevicemessage->setactivetimedevicerequest;
        ESP_LOGI(TAG, "Has active time request from %d:%d to %d:%d ",
            request->activelamp->begin->hour,
            request->activelamp->begin->minute,
            request->activelamp->end->hour,
            request->activelamp->end->minute
        );
        active_time_t active_time = {
            .start_hour = request->activelamp->begin->hour,
            .start_minute = request->activelamp->begin->minute,
            .end_hour = request->activelamp->end->hour,
            .end_minute = request->activelamp->end->minute,
        };
        _callback(handle, WEBSOCKET_ON_SETTING_ACTIVE_TIME, &active_time, sizeof(active_time_t));
    } else if (mainMessage->devicemessage &&
        mainMessage->devicemessage->userdevicemessage &&
        mainMessage->devicemessage->userdevicemessage->setcycleconfigdevicerequest) {
        SetCycleConfigDeviceRequest* request = mainMessage->devicemessage->userdevicemessage->setcycleconfigdevicerequest;
        ESP_LOGI(TAG, "has cycle config %d %d %d %d %d", request->numphase, request->greentime[0], request->greentime[1], request->yellowtime, request->clearancetime);
    } else if (mainMessage->devicemessage &&
        mainMessage->devicemessage->userdevicemessage &&
        mainMessage->devicemessage->userdevicemessage->forceotaupdateuserrequest) {
        ESP_LOGI(TAG, "Has force OTA update request");
        _callback(handle, WEBSOCKET_ON_FORCE_OTA, NULL, 0);
    } else if (mainMessage->devicemessage &&
        mainMessage->devicemessage->userdevicemessage &&
        mainMessage->devicemessage->userdevicemessage->releasecontroluserrequest) {
        ESP_LOGI(TAG, "Release control");
        _callback(handle, WEBSOCKET_ON_FREE, NULL, 0);
    }
    return ESP_OK;
}

void _rx_process_task(void* pv) {
    app_websocket_handle_t handle = (app_websocket_handle_t)pv;

    size_t item_sz;
    
    while (1) {

        void *item = xRingbufferReceive(handle->rx_rb, &item_sz, 100 / portTICK_RATE_MS);

        if (item != NULL) {
            _process_data(handle, item, item_sz);
            vRingbufferReturnItem(handle->rx_rb, item);
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);        
    }
}

void app_websocket_start(app_websocket_handle_t handle) {
    if (handle == NULL) {
        ESP_LOGE(TAG, "handle was null");
        return;
    }
    if (xTaskCreate(_websocket_task, "_websocket_task", 1024 * 8, handle, 5, NULL) != pdTRUE) {
        ESP_LOGE(TAG, "Error create console task, memory exhausted?");
        return;
    }
    if (xTaskCreate(_rx_process_task, "_rx_process_task", 1024 * 8, handle, 5, NULL) != pdTRUE) {
        ESP_LOGE(TAG, "Error create console task, memory exhausted?");
        return;
    }
}

void app_websocket_send(app_websocket_handle_t handle, uint8_t* data, int len) {
    esp_websocket_client_send_bin(handle->client, (char*) data, len, 1000 / portTICK_PERIOD_MS);
}

esp_err_t app_websocket_update_power_state(app_websocket_handle_t handle, bool is_on) {
    if (handle == NULL) {
        return ESP_FAIL;
    }
    handle->power_state = is_on;
    if (handle->is_logged) {
        _update_power_state(handle, is_on);
    }
    return ESP_OK;
}

esp_err_t app_websocket_update_mode(app_websocket_handle_t handle, int mode) {
    if (handle == NULL) {
        return ESP_FAIL;
    }
    handle->mode = mode;
    if (handle->is_logged) {
        _update_mode(handle, mode);
    }
    return ESP_OK;
}

esp_err_t app_websocket_update_meter_data(app_websocket_handle_t handle, phases_data_t* phases_data) {
    if (handle == NULL || phases_data == NULL) {
        return ESP_FAIL;
    }
    if (handle->is_logged) {
        _update_meter_data(handle, phases_data);
    }
    return ESP_OK;
}

esp_err_t app_websocket_update_active_time(app_websocket_handle_t handle, active_time_t active_time) {
    if (handle == NULL) {
        return ESP_FAIL;
    }
    memcpy(&handle->active_time, &active_time, sizeof(active_time_t));
    if (handle->is_logged) {
        _update_active_time(handle, active_time);
    }
    return ESP_OK;
}