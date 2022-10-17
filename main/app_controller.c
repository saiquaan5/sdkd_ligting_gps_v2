#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include "stdint.h"
#include "app_controller.h"
#include "driver/gpio.h"

#define TAG     "APP_CONTROLLER"

typedef struct app_controller_ {
    int relay_1_pin;
    int relay_2_pin;
    int switcher_auto_pin;
    int switcher_manual_pin;
    int status_pin;
    app_callback_t* callback;
    int switcher_state;
    app_controller_mode_t mode;
    int relay_state;
} app_controller_t;

void _gpio_task(void* pv) {
    app_controller_handle_t handle = (app_controller_handle_t)pv;

    int manual_state = 1;
    int auto_state = 1;
    app_controller_mode_t mode = APP_CONTROLLER_INPUT_MODE_OFF;
    app_controller_mode_t cur_mode = APP_CONTROLLER_INPUT_MODE_OFF;

    int status_state = 0;

    int log_heap_tick = 0;

    while (1) {
        manual_state = gpio_get_level(handle->switcher_manual_pin);
        auto_state = gpio_get_level(handle->switcher_auto_pin);
        if (auto_state == 0 && manual_state == 1) {
            cur_mode = APP_CONTROLLER_INPUT_MODE_AUTO;
        } else if (auto_state == 1 && manual_state == 0) {
            cur_mode = APP_CONTROLLER_INPUT_MODE_MANUAL;
        } else {
            cur_mode = APP_CONTROLLER_INPUT_MODE_OFF;
        }
        if (cur_mode != mode) {
            mode = cur_mode;
            handle->mode = mode;
            if (handle->callback != NULL) {
                handle->callback(APP_CONTROLLER_EVENT_CHANGE_MODE, &mode, sizeof(int));
            }
        }
        gpio_set_level(handle->status_pin, status_state);
        status_state = status_state == 0 ? 1 : 0;
        log_heap_tick++;
        if (log_heap_tick > 20) {
            ESP_LOGD(TAG, "free heap %d", esp_get_free_heap_size());
            log_heap_tick = 0;
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

app_controller_handle_t app_controller_init(app_controller_config_t* config) {
    app_controller_handle_t handle = malloc(sizeof(app_controller_t));

    handle->callback = config->callback;
    handle->relay_1_pin = config->relay_1_pin;
    handle->relay_2_pin = config->relay_2_pin;
    handle->switcher_manual_pin = config->switcher_manual_pin;
    handle->switcher_auto_pin = config->switcher_auto_pin;
    handle->switcher_state = 0;
    handle->status_pin = config->status_pin;

    gpio_config_t io_conf;
    //disable interrupt
    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf.pin_bit_mask = ((1ULL<<(handle->relay_1_pin)) | (1ULL<<(handle->relay_2_pin)) | (1ULL<<(handle->status_pin)));
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //disable pull-up mode
    io_conf.pull_up_en = 0;
    //configure GPIO with the given settings
    gpio_config(&io_conf);

    //interrupt of rising edge
    io_conf.intr_type = GPIO_PIN_INTR_ANYEDGE;
    //bit mask of the pins, use GPIO4/5 here
    io_conf.pin_bit_mask = (1ULL << (handle->switcher_auto_pin) | (1ULL<<(handle->switcher_manual_pin)));
    //set as input mode    
    io_conf.mode = GPIO_MODE_INPUT;
    //enable pull-up mode
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);

    handle->relay_state = 0;

    xTaskCreate(_gpio_task, "_gpio_task", 2048, handle, 10, NULL);
    
    return handle;
}

esp_err_t app_controller_control_relay(app_controller_handle_t handle, bool is_active) {
    if (handle == NULL) {
        return ESP_FAIL;
    }
    if ((handle->relay_state > 0 && is_active) || (handle->relay_state == 0 && !is_active)) {
        return ESP_OK;
    }
    // if (handle->mode == APP_CONTROLLER_INPUT_MODE_OFF) {
    //     ESP_LOGD(TAG, "mode off");
    //     return ESP_OK;
    // }
    handle->relay_state = is_active ? 1 : 0;
    gpio_set_level(handle->relay_1_pin, handle->relay_state);
    gpio_set_level(handle->relay_2_pin, handle->relay_state);
    if (handle->callback != NULL) {
        handle->callback(APP_CONTROLLER_EVENT_CHANGE_POWER, &is_active, sizeof(int));
    }
    return ESP_OK;
}

esp_err_t app_controller_force_manual(app_controller_handle_t handle, bool is_release) {
    if (handle == NULL) {
        return ESP_FAIL;
    }
    app_controller_mode_t mode = is_release == false ? APP_CONTROLLER_INPUT_MODE_MANUAL : handle->mode;
    if (handle->callback != NULL) {
        handle->callback(APP_CONTROLLER_EVENT_CHANGE_MODE, &mode, sizeof(int));
    }
    return ESP_OK;
}

bool app_controller_get_switcher_state(app_controller_handle_t handle);

app_controller_mode_t app_controller_get_mode(app_controller_handle_t handle) {
    if (handle == NULL) {
        return APP_CONTROLLER_INPUT_MODE_OFF;
    }

    return handle->mode;
}