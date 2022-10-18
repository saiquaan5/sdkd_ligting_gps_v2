#ifndef APP_GPS_H
#define APP_GPS_H

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "app_common.h"
#include "app_controller.h"
#include "app_network.h"
#include "app_schedule.h"


typedef enum {
    APP_GPS_EVENT_SET_TIME = APP_GPS_EVENT_BASE + 1,
} app_gps_event_id_t;

#define BUF_SIZE 1024

typedef struct {
    uart_config_t uart_config;
    int uartPort; // Cau hinh port 1
    int tx_pin;
    int rx_pin;
    app_callback_t* callback;
    app_network_handle_t network_handle;
    app_schedule_handle_t schedule;

    long last_gps_tick;
} app_gps_config_t;


typedef struct app_gps_{
    int uartPort; // Cau hinh port 1
    int tx_pin;
    int rx_pin;
    bool is_get_gps;
    long last_get_tick_gps;
    app_callback_t* callback;
    app_network_handle_t network_handle;
    app_schedule_handle_t schedule;
    bool is_sync_rtc_gps;
} app_gps_t;

date_time_form_t date_time_form;
typedef struct app_gps_ *app_gps_handle_t;

app_gps_handle_t app_gps_init(app_gps_config_t* config);
static void _callback(app_gps_handle_t handle, int event_id, void* data, int data_len);
void app_gps_start(app_gps_handle_t handle);


#endif