#include "app_common.h"
#include "app_nextion.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_sntp.h"

#define BUF_SIZE 1024

const char* MANUAL_ON_KEY = "@manual_on$";
const char* MANUAL_OFF_KEY = "@manual_off$";
const char* UP_TIME = "@up_time";
const char* PAGE_PREFIX = "@page_";
const char* RTC_PREFIX = "@rtc";
const char* SYNC_TIME = "@time_syn_datetime$";
const uint8_t SLEEP[4] = {0x86, 0xff, 0xff, 0xff};

const char* OFF_TEXT = "TẮT";
const char* AUTO_TEXT = "TỰ ĐỘNG";

#define TAG "APP_NEXTION"

typedef struct app_nextion_ {
    int port_num;
    int tx_pin;
    int rx_pin;
    int baudrate;
    app_callback_t* callback;
    uint8_t* tx_buffer;
    int current_page;
    bool is_connected;
    bool is_on;
    active_time_t active_time;
    phases_data_t phases_data;
    app_controller_mode_t mode;
    char* mode_txt;
    bool is_sleeping;
    char* hardware_id;
} app_nextion_t;

void _send_command(app_nextion_handle_t handle, uint8_t* data, int len) {
    // ESP_LOGD(TAG, "send %s", (char*)data);
    uart_write_bytes(handle->port_num, (const char*) data, len);
    uint8_t end = 0xff;
    uart_write_bytes(handle->port_num, (const char*) &end, 1);
    uart_write_bytes(handle->port_num, (const char*) &end, 1);
    uart_write_bytes(handle->port_num, (const char*) &end, 1);
}

app_nextion_handle_t app_nextion_init(app_nextion_config_t* config) {
    app_nextion_handle_t handle = malloc(sizeof(app_nextion_t));

    handle->baudrate = config->baudrate;
    handle->tx_pin = config->tx_pin;
    handle->rx_pin = config->rx_pin;
    handle->port_num = config->port_num;
    handle->callback = config->callback;

    handle->tx_buffer = malloc(BUF_SIZE);

    handle->is_connected = false;
    handle->is_on = false;

    handle->mode = APP_CONTROLLER_INPUT_MODE_OFF;

    handle->mode_txt = OFF_TEXT;

    handle->is_sleeping = false;

    handle->hardware_id = strdup(config->hardware_id);

    memset(&handle->active_time, 0, sizeof(active_time_t));
    memset(&handle->phases_data, 0, sizeof(phases_data_t));

    memcpy(&handle->active_time, &config->init_active_time, sizeof(active_time_t));

    return handle;
}

static void _callback(app_nextion_handle_t handle, int event_id, void* data, int len) {
    if (handle->callback != NULL) {
        handle->callback(event_id, data, len);
    }
}

void _print_hex(uint8_t* data, int len) {
    for (int i = 0; i < len; i++) {
        printf("%02X ", data[i]);
    }
    printf("\n");
}

void _nextion_read_task(void* pv) {
    app_nextion_handle_t handle = (app_nextion_handle_t)pv;

    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    uart_config_t uart_config = {
        .baud_rate = handle->baudrate,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    uart_driver_install(handle->port_num, BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(handle->port_num, &uart_config);
    uart_set_pin(handle->port_num, handle->tx_pin, handle->rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    ESP_LOGI(TAG, "%d, %d, %d", handle->port_num, handle->tx_pin, handle->rx_pin);

    // Configure a temporary buffer for the incoming data
    uint8_t *data = (uint8_t *) malloc(BUF_SIZE);

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    while (1) {
        int len = uart_read_bytes(handle->port_num, data, BUF_SIZE, 20 / portTICK_RATE_MS);
        if (len <= 0) {
            continue;
        }
        _print_hex(data, len);
        ESP_LOGD(TAG, "has %d bytes from screen %.*s %02X", len, len, (char*) data, data[0]);
        if (memcmp(data, MANUAL_ON_KEY, strlen(MANUAL_ON_KEY)) == 0) {
            ESP_LOGD(TAG, "manual on");
            _callback(handle, APP_NEXTION_EVENT_MANUAL_ON, NULL, 0);
        } else if (memcmp(data, MANUAL_OFF_KEY, strlen(MANUAL_OFF_KEY)) == 0) {
            ESP_LOGD(TAG, "manual off");
            _callback(handle, APP_NEXTION_EVENT_MANUAL_OFF, NULL, 0);
        } else if (memcmp(data, UP_TIME, strlen(UP_TIME)) == 0) {
            int start_hour, start_minute, end_hour, end_minute;
            sscanf((const char*)data, "@up_time,%d,%d,%d,%d,$", &start_hour, &start_minute, &end_hour, &end_minute);

            ESP_LOGD(TAG, "Got set time %02d:%02d - %02d:%02d", start_hour, start_minute, end_hour, end_minute);

            if (start_hour > 23 || start_minute > 59 || end_hour > 23 || end_minute > 59) {
                ESP_LOGE(TAG, "Invalid setting time");
                app_nextion_set_console(handle, "Thời gian không hợp lệ");
                continue;
            }

            handle->active_time.start_hour = start_hour;
            handle->active_time.start_minute = start_minute;
            handle->active_time.end_hour = end_hour;
            handle->active_time.end_minute = end_minute;

            _callback(handle, APP_NEXTION_EVENT_SET_SCHEDULE, &handle->active_time, sizeof(active_time_t));
        } else if (memcmp(data, PAGE_PREFIX, strlen(PAGE_PREFIX)) == 0) {
            int page = 0;
            sscanf((const char*)data, "@page_%d", &page);

            ESP_LOGD(TAG, "got page %d", page);

            if (page == PAGE_HOME_INDEX) {
                if (handle->is_sleeping) {
                    handle->current_page = PAGE_LOCK_INDEX;
                    app_nextion_set_page(handle, PAGE_LOCK_INDEX);
                    handle->is_sleeping = false;
                    continue;
                } else if (handle->mode == APP_CONTROLLER_INPUT_MODE_MANUAL) {
                    handle->current_page = PAGE_MANUAL_INDEX;
                    app_nextion_set_page(handle, PAGE_MANUAL_INDEX);
                    continue;
                }
            }

            handle->current_page = page;
        } else if (memcmp(data, RTC_PREFIX, strlen(RTC_PREFIX)) == 0) {
            date_time_form_t form;
            memset(&form, 0, sizeof(date_time_form_t));

            sscanf((const char*)data, "@rtc,%d,%d,%d,%d,%d,$", &form.hour, &form.minute, &form.day, &form.month, &form.year);

            ESP_LOGD(TAG, "got set date time: %02d:%02d %02d/%02d/%04d", form.hour, form.minute, form.day, form.month, form.year);

            _callback(handle, APP_NEXTION_EVENT_SET_TIME, &form, sizeof(date_time_form_t));
        } else if (memcmp(data, SYNC_TIME, strlen(SYNC_TIME)) == 0) {
            ESP_LOGD(TAG, "Has sync time request");

            _callback(handle, APP_NEXTION_EVENT_SYNC_TIME, NULL, 0);
        } else if (memcmp(data, SLEEP, sizeof(SLEEP)) == 0) {
            ESP_LOGD(TAG, "Screen go to sleep");

            handle->is_sleeping = true;
        }
    }
}

float f_abs(float data) {
    return data < 0 ? -data : data;
}

void _nextion_write_task(void* pv) {
    app_nextion_handle_t handle = (app_nextion_handle_t) pv;

    char time_c[50];
    char date_c[50];

    char* send_buffer = malloc(1024);

    int len = 0;

    vTaskDelay(2000 / portTICK_PERIOD_MS);

    len = sprintf(send_buffer, "sleep=0");
    _send_command(handle, (uint8_t*) send_buffer, len);

    app_nextion_set_page(handle, PAGE_LOADING_INDEX);

    handle->is_sleeping = false;

    handle->current_page = PAGE_LOADING_INDEX;

    app_nextion_set_loading(handle, 25, "Đang khởi động");

    bool page_time_flag = false;
    bool page_schedule_flag = false;

    while (1) {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);

        char strftime_buf[64];
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TAG, "The current date/time system is: %s", strftime_buf);
        
        sprintf(time_c, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        sprintf(date_c, "%02d/%02d/%4d", timeinfo.tm_mday, timeinfo.tm_mon + 1, (timeinfo.tm_year + 1900));

        // ESP_LOGI(TAG, "date time %s %s", time_c, date_c);

        if (handle->current_page == PAGE_LOCK_INDEX) {
            page_time_flag = false;
            page_schedule_flag = false;
            len = sprintf(send_buffer, "rtcTime.txt=\"%s\"", time_c);
            _send_command(handle, (uint8_t*) send_buffer, len);
            len = sprintf(send_buffer, "t4.txt=\"%s\"", date_c);
            _send_command(handle, (uint8_t*) send_buffer, len);
        } else if (handle->current_page == PAGE_HOME_INDEX) {
            page_time_flag = false;
            page_schedule_flag = false;
            len = sprintf(send_buffer, "t1.txt=\"%s\"", handle->mode_txt);
            _send_command(handle, (uint8_t*) send_buffer, len);

            len = sprintf(send_buffer, "rtcTime.txt=\"%s\"", time_c);
            _send_command(handle, (uint8_t*) send_buffer, len);
            
            len = sprintf(send_buffer, "t4.txt=\"%s\"", date_c);
            _send_command(handle, (uint8_t*) send_buffer, len);

            len = sprintf(send_buffer, "t2.txt=\"%s\"", handle->is_connected ? "Đang kết nối" : "Mất kết nối");
            _send_command(handle, (uint8_t*) send_buffer, len);

            len = sprintf(
                send_buffer,
                "t3.txt=\"Hoạt Động: %02d:%02d - %02d:%02d\"",
                handle->active_time.start_hour,
                handle->active_time.start_minute,
                handle->active_time.end_hour,
                handle->active_time.end_minute
            );
            _send_command(handle, (uint8_t*) send_buffer, len);

            len = sprintf(
                send_buffer,
                "t6.txt=\"Pha 1: %06.2fKW %05.2fV %05.1fA %04.2f\"",
                handle->phases_data.p1,
                handle->phases_data.u1,
                handle->phases_data.i1,
                f_abs(handle->phases_data.phy1)
            );
            _send_command(handle, (uint8_t*) send_buffer, len);
            vTaskDelay(50 / portTICK_PERIOD_MS);

            len = sprintf(
                send_buffer,
                "t7.txt=\"Pha 2: %06.2fKW %05.2fV %05.1fA %04.2f\"",
                handle->phases_data.p2,
                handle->phases_data.u2,
                handle->phases_data.i2,
                f_abs(handle->phases_data.phy2)
            );
            _send_command(handle, (uint8_t*) send_buffer, len);
            vTaskDelay(50 / portTICK_PERIOD_MS);

            len = sprintf(
                send_buffer,
                "t8.txt=\"Pha 3: %06.2fKW %05.2fV %05.1fA %04.2f\"",
                handle->phases_data.p3,
                handle->phases_data.u3,
                handle->phases_data.i3,
                f_abs(handle->phases_data.phy3)
            );
            _send_command(handle, (uint8_t*) send_buffer, len);

            // len = sprintf(send_buffer, "t5.txt=\"Thong bao: %f %f\"", handle->phases_data.u2, handle->phases_data.u1);
            // _send_command(handle, (uint8_t*) send_buffer, len);

            vTaskDelay(50 / portTICK_PERIOD_MS);
        } else if (handle->current_page == PAGE_CONNECT_INDEX) {
            page_time_flag = false;
            page_schedule_flag = false;
            len = sprintf(send_buffer, "t2.txt=\"%s\"", handle->hardware_id);
            _send_command(handle, (uint8_t*) send_buffer, len);

            len = sprintf(send_buffer, "t4.txt=\"%s\"", FIRMWARE_VERSION);
            _send_command(handle, (uint8_t*) send_buffer, len);

            len = sprintf(send_buffer, "t6.txt=\"%s\"", "112233445");
            _send_command(handle, (uint8_t*) send_buffer, len);
        } else if (handle->current_page == PAGE_SCHEDULE_INDEX) {
            if (!page_time_flag) {
                len = sprintf(send_buffer, "n0.val=%d", handle->active_time.start_hour);
                _send_command(handle, (uint8_t*) send_buffer, len);

                len = sprintf(send_buffer, "n1.val=%d", handle->active_time.start_minute);
                _send_command(handle, (uint8_t*) send_buffer, len);

                len = sprintf(send_buffer, "n2.val=%d", handle->active_time.end_hour);
                _send_command(handle, (uint8_t*) send_buffer, len);

                len = sprintf(send_buffer, "n3.val=%d", handle->active_time.end_minute);
                _send_command(handle, (uint8_t*) send_buffer, len);
            }
            page_time_flag = true;
            page_schedule_flag = false;
        } else if (handle->current_page == PAGE_TIME_INDEX) {
            if (!page_schedule_flag) {
                len = sprintf(send_buffer, "n0.val=%d", timeinfo.tm_hour);
                _send_command(handle, (uint8_t*) send_buffer, len);

                len = sprintf(send_buffer, "n1.val=%d", timeinfo.tm_min);
                _send_command(handle, (uint8_t*) send_buffer, len);

                len = sprintf(send_buffer, "n2.val=%d", timeinfo.tm_mday);
                _send_command(handle, (uint8_t*) send_buffer, len);

                len = sprintf(send_buffer, "n3.val=%d", timeinfo.tm_mon + 1);
                _send_command(handle, (uint8_t*) send_buffer, len);

                len = sprintf(send_buffer, "n4.val=%d", timeinfo.tm_year + 1900);
                _send_command(handle, (uint8_t*) send_buffer, len);
            }
            page_time_flag = false;
            page_schedule_flag = true;
        } else {
            page_time_flag = false;
            page_schedule_flag = false;
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

esp_err_t app_nextion_start(app_nextion_handle_t handle) {
    if (handle == NULL) {
        return ESP_FAIL;
    }

    xTaskCreate(_nextion_read_task, "_nextion_read_task", 1024 * 6, handle, 10, NULL);
    xTaskCreate(_nextion_write_task, "_nextion_write_task", 1024 * 6, handle, 10, NULL);

    vTaskDelay(1000 / portTICK_PERIOD_MS);
    return ESP_OK;
}

void app_nextion_set_page(app_nextion_handle_t handle, int page_num) {
    if (handle == NULL) {
        return;
    }
    int len = sprintf((char*) handle->tx_buffer, "page page%d", page_num);
    _send_command(handle, handle->tx_buffer, len);
}

void app_nextion_set_console(app_nextion_handle_t handle, const char* message) {
    int len = sprintf((char*) handle->tx_buffer, "console.txt=\"%s\"", message);
    _send_command(handle, handle->tx_buffer, len);
}

void app_nextion_set_button(app_nextion_handle_t handle, bool is_on) {
    int len = sprintf((char*) handle->tx_buffer, "bt0.val=%d", is_on ? 1 : 0);
    _send_command(handle, handle->tx_buffer, len);
}

esp_err_t app_nextion_set_conection_state(app_nextion_handle_t handle, bool is_connected) {
    if (handle == NULL) {
        return ESP_FAIL;
    }
    handle->is_connected = is_connected;
    return ESP_OK;
}

esp_err_t app_nextion_set_power_state(app_nextion_handle_t handle, bool is_on) {
    if (handle == NULL) {
        return ESP_FAIL;
    }
    handle->is_on = is_on;
    return ESP_OK;
}

esp_err_t app_nextion_set_phase_data(app_nextion_handle_t handle, phases_data_t* phase_data) {
    if (handle == NULL || phase_data == NULL) {
        return ESP_FAIL;
    }
    memcpy(&handle->phases_data, phase_data, sizeof(phases_data_t));
    app_nextion_set_console(handle, "Cập nhật thông số điện thành công");
    return ESP_OK;
}

esp_err_t app_nextion_set_active_time(app_nextion_handle_t handle, active_time_t* active_time) {
    if (handle == NULL || active_time == NULL) {
        return ESP_FAIL;
    }
    memcpy(&handle->active_time, active_time, sizeof(active_time_t));
    return ESP_OK;
}

esp_err_t app_nextion_set_mode(app_nextion_handle_t handle, app_controller_mode_t mode) {
    if (handle == NULL) {
        return ESP_FAIL;
    }
    handle->mode = mode;
    switch (handle->mode)
    {
    case APP_CONTROLLER_INPUT_MODE_OFF:
        handle->mode_txt = OFF_TEXT;
        if (handle->current_page == PAGE_MANUAL_INDEX) {
            app_nextion_set_page(handle, PAGE_HOME_INDEX);
        }
        break;
    case APP_CONTROLLER_INPUT_MODE_AUTO:
        handle->mode_txt = AUTO_TEXT;
        if (handle->current_page == PAGE_MANUAL_INDEX) {
            app_nextion_set_page(handle, PAGE_HOME_INDEX);
        }
        break;
    case APP_CONTROLLER_INPUT_MODE_MANUAL:
        if (handle->current_page != PAGE_LOCK_INDEX && handle->current_page != PAGE_LOADING_INDEX) {
            app_nextion_set_page(handle, PAGE_MANUAL_INDEX);
        }
    default:
        break;
    }
    return ESP_OK;
}

esp_err_t app_nextion_set_loading(app_nextion_handle_t handle, int percent, char* console) {
    if (handle == NULL) {
        return ESP_FAIL;
    }
    int len = sprintf((char*) handle->tx_buffer, "j0.val=%d", percent);
    _send_command(handle, handle->tx_buffer, len);
    if (console != NULL) {
        len = sprintf((char*) handle->tx_buffer, "t2.txt=\"%s\"", console);
        _send_command(handle, handle->tx_buffer, len);
    }
    return ESP_OK;
}