#ifndef _APP_NETWORK_H_
#define _APP_NETWORK_H_

#include <string.h>
#include "freertos/FreeRTOS.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

typedef struct {
    bool is_using_wifi;
} app_network_config_t;

typedef struct app_network_* app_network_handle_t;

app_network_handle_t app_network_init(app_network_config_t* config);

void app_network_start(app_network_handle_t handle);

bool app_network_is_connected(app_network_handle_t handle);

#endif