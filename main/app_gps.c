#include "app_gps.h"
#include "app_network.h"
#include "app_common.h"
#include "app_schedule.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "string.h"
#include "app_schedule.h"

#define TAG "GPS_TASK"


#define ECHO_TEST_TXD (GPIO_NUM_17)
#define ECHO_TEST_RXD (GPIO_NUM_16)
#define ECHO_TEST_RTS (UART_PIN_NO_CHANGE)
#define ECHO_TEST_CTS (UART_PIN_NO_CHANGE)

// #define BUF_SIZE (1024)

static void _callback(app_gps_handle_t handle, int event_id, void *data, int data_len)
{
    if (handle->callback != NULL)
    {
        handle->callback(event_id, data, data_len);
    }
}

app_gps_handle_t app_gps_init(app_gps_config_t *config)
{
    app_gps_handle_t handle = malloc(sizeof(app_gps_handle_t));
    handle->callback = config->callback;
    handle->is_get_gps = true;
    handle->last_get_tick_gps = 30 * 1000 * 2 * 10;
    handle->rx_pin = config->rx_pin;
    handle->tx_pin = config->tx_pin;
    // handle->uart_config = config->uart_config;
    handle->uartPort = config->uartPort;
    handle->last_get_tick_gps = 0;
    handle->schedule = config->schedule;
    handle->is_sync_rtc_gps = false;
    handle->callback = config->callback;
    return handle;
}

void switchingUartPortToGps(void)
{
    gpio_reset_pin(GPS_ACTIVE);
    gpio_reset_pin(GPS_RST);
    gpio_reset_pin(S0_MUX);
    gpio_reset_pin(S1_MUX);
    gpio_reset_pin(PWR_SIM);

    gpio_set_direction(GPS_ACTIVE, GPIO_MODE_OUTPUT);
    gpio_set_direction(GPS_RST, GPIO_MODE_OUTPUT);
    gpio_set_direction(S0_MUX, GPIO_MODE_OUTPUT);
    gpio_set_direction(S1_MUX, GPIO_MODE_OUTPUT);
    gpio_set_direction(PWR_SIM, GPIO_MODE_OUTPUT);

    gpio_set_level(PWR_SIM, 0); // Tat nguon sim
    vTaskDelay(100 / portTICK_PERIOD_MS);
    gpio_set_level(GPS_ACTIVE, 1); // Kich muc cao ra app
    gpio_set_level(GPS_RST, 0);    // Muc cao reset

    // swich qua cong uart gps
    gpio_set_level(S0_MUX, 0);
    gpio_set_level(S1_MUX, 0);
}

void _gps_task(void *pv)
{
    app_gps_handle_t handle = (app_gps_handle_t)pv;
    while (1)
    {
        if (esp_tick_get() - handle->last_get_tick_gps <  30*1000)
        {
            vTaskDelay(10 / portTICK_PERIOD_MS);
            continue;
        }
        handle->last_get_tick_gps = esp_tick_get();
        ESP_LOGI(TAG, "lay du lieu gps");
        switchingUartPortToGps();
        uart_config_t uart_config = {
            .baud_rate = 9600,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_APB,
        };
        uart_driver_install(UART_NUM_1, BUF_SIZE * 2, 0, 0, NULL, 0);
        uart_param_config(UART_NUM_1, &uart_config);
        uart_set_pin(UART_NUM_1, ECHO_TEST_TXD, ECHO_TEST_RXD, ECHO_TEST_RTS, ECHO_TEST_CTS);
        uint8_t *data = (uint8_t *)malloc(BUF_SIZE);
        bool is_get_gps = true;
        uint8_t couter_check_gps_fix = 0;
        while (is_get_gps)
        {
            if (esp_tick_get() - handle->last_get_tick_gps < 10 * 1000)
            {
                int len = uart_read_bytes(UART_NUM_1, data, (BUF_SIZE - 1), 20 / portTICK_PERIOD_MS);
                if (len)
                {
                    data[len] = '\0';
                    // ESP_LOGI(TAG, "Recv str: %s", (char *) data);
                    if (strstr((char *)data, "GNRMC") != NULL)
                    {
                        uint8_t *data_gps_rtc = (uint8_t *)malloc(BUF_SIZE);
                        strcpy((char *)data_gps_rtc, (char *)data);
                        // ESP_LOGI(TAG, "%s", (char *)data_gps_rtc);

                        char *string_time = malloc(sizeof(char) * 20);
                        char *string_date = malloc(sizeof(char) * 20);
                        // char string_date[20];

                        char *temp = strtok((char *)data_gps_rtc, "$");
                        temp = strtok(NULL, ",");
                        temp = strtok(NULL, ",");
                        strcpy(string_time, temp);
                        // printf("\r\n%s\r\n", string_time);

                        temp = strtok(NULL, ",");
                        if (strstr(temp, "V"))
                        {
                            ESP_LOGI(TAG, "GPS not fix");
                        }
                        else if (strstr(temp, "A"))
                        {
                            couter_check_gps_fix++;
                            ESP_LOGI(TAG, "GPS fixed");
                            temp = strtok(NULL, ",");
                            // Doc du lieu cua GPS
                            double latitude = atof(temp);
                            temp = strtok(NULL, ",");
                            temp = strtok(NULL, ",");
                            double longitude = atof(temp);
                            temp = strtok(NULL, ",");
                            temp = strtok(NULL, ",");
                            temp = strtok(NULL, ",");
                            temp = strtok(NULL, ",");
                            strcpy(string_date, temp);

                            uint8_t gps_h = 0, gps_m = 0, gps_s = 0, gps_date = 0, gps_month = 0;
                            uint32_t gps_yr = 0;

                            gps_h = (string_time[0] - '0') * 10;
                            gps_h += (string_time[1] - '0');

                            gps_m = (string_time[2] - '0') * 10;
                            gps_m += (string_time[3] - '0');
                            gps_s = (string_time[4] - '0') * 10;
                            gps_s += (string_time[5] - '0');

                            gps_date = (string_date[0] - '0') * 10;
                            gps_date += (string_date[1] - '0');
                            gps_month = (string_date[2] - '0') * 10;
                            gps_month += (string_date[3] - '0');
                            gps_yr = (string_date[4] - '0') * 10;
                            gps_yr += (string_date[5] - '0');
                            gps_yr += 2000;
                            gps_h = gps_h + 7;
                            if (gps_h > 23)
                            {
                                gps_h = gps_h - 24;
                            }
                            ESP_LOGI(TAG, "Gps convert: %02d:%02d:%02d  %02d/%02d/%04d", gps_h,
                                     gps_m,
                                     gps_s,
                                     gps_date,
                                     gps_month,
                                     gps_yr);
                            if (couter_check_gps_fix == 2)
                            {

                                handle->is_sync_rtc_gps = true;
                                couter_check_gps_fix = 0;
                                is_get_gps = false;
                                memset(&date_time_form, 0, sizeof(date_time_form_t));
                                date_time_form.hour = gps_h;
                                date_time_form.minute = gps_m;
                                date_time_form.day = gps_date;
                                date_time_form.month = gps_month;
                                date_time_form.year = gps_yr;
                                date_time_form.second = gps_s;
                            }
                        }
                        free(data_gps_rtc);
                        free(string_date);
                        free(string_time);
                        // free(data);
                    }
                }
                continue;
            }
            handle->last_get_tick_gps = esp_tick_get();
            is_get_gps = false;
        }
    }
}
void _GPS_process_task(void *pv)
{
    app_gps_handle_t handle = (app_gps_handle_t )pv;

    while (1)
    {
        if(handle->is_sync_rtc_gps)
        {

            // ESP_LOGI(TAG,"sync gps data rtc");
            // ESP_LOGI(TAG, "Gps convert: %02d:%02d:%02d  %02d/%02d/%04d", date_time_form_gps.hour,
            //          date_time_form_gps.minute,
            //          0,
            //          date_time_form_gps.day,
            //          date_time_form_gps.month,
            //          date_time_form_gps.year);
            // app_schedule_set_time(handle->schedule, date_time_form_gps);
            // _callback(handle, APP_GPS_EVENT_SET_TIME, &handle->date_time_form,sizeof(date_time_form_t));
            _callback(handle, APP_GPS_EVENT_SET_TIME, &date_time_form, sizeof(date_time_form_t));
            handle->is_sync_rtc_gps = false;
            continue;
        }
        vTaskDelay(10/ portTICK_PERIOD_MS);
    }
}

void app_gps_start(app_gps_handle_t handle)
{
    if (handle == NULL)
    {
        ESP_LOGE(TAG, "handle was null");
        return;
    }

    if (xTaskCreate(_gps_task, "_gps_task", 1024 * 5, handle, 5, NULL) != pdTRUE)
    {
        ESP_LOGE(TAG, "Error create console task, memory exhausted?");
        return;
    }
    if (xTaskCreate(_GPS_process_task, "_rx_process_task", 1024 * 5, handle, 5, NULL) != pdTRUE)
    {
        ESP_LOGE(TAG, "Error create console task, memory exhausted?");
        return;
    }
}
