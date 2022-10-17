#ifndef _APP_MODBUS_H_
#define _APP_MODBUS_H_

#include <string.h>
#include "freertos/FreeRTOS.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include "app_common.h"

typedef enum {
    APP_MODBUS_EVENT_DATA = APP_MODBUS_EVENT_BASE + 1,
} app_modbus_event_id_t;

typedef struct {
    int update_interval_ms;
    uint8_t port_num;
    int tx_pin;
    int rx_pin;
    int rst_pin;
    int baudrate;
    app_callback_t* callback;
} app_modbus_config_t;

typedef struct app_modbus_* app_modbus_handle_t;

app_modbus_handle_t app_modbus_init(app_modbus_config_t* config);

esp_err_t app_modbus_start(app_modbus_handle_t handle);

#endif