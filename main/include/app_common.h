#ifndef _APP_CONFIG_H_
#define _APP_CONFIG_H_

#include <string.h>
#include "freertos/FreeRTOS.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_log.h"

#define APP_WEBSOCKET_EVENT_BASE    (0)
#define APP_SCHEDULE_EVENT_BASE     (100)
#define APP_NEXTION_EVENT_BASE      (200)
#define APP_MODBUS_EVENT_BASE       (300)
#define APP_CONTROLLER_EVENT_BASE   (400)

#define FIRMWARE_VERSION            "1.1.0"

#define RESTART_HOUR                12
#define RESTAR_MINUTE               0

#define HTTP_HOST                   "http://192.168.1.255:8443"
#define WS_HOST                     "ws://192.168.1.255:8443"

#define AWAIT(a) if (a != ESP_OK) continue

typedef struct {
    int start_hour;
    int start_minute;
    int end_hour;
    int end_minute;
} active_time_t;

typedef struct {
    float p1;
    float p2;
    float p3;
    float u1;
    float u2;
    float u3;
    float i1;
    float i2;
    float i3;
    float phy1;
    float phy2;
    float phy3;
    float power;
} phases_data_t;

typedef struct {
    int hour;
    int minute;
    int day;
    int month;
    int year;
} date_time_form_t;

typedef enum {
    OPERATOR_MODE_OFF = 0,
    OPERATOR_MODE_AUTO,
    OPERATOR_MODE_MANUAL,
} operator_mode_t;

typedef void(app_callback_t)(int event_id, void* data, int data_len);

long long esp_tick_get();

#endif