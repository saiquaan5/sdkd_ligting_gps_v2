#include "app_schedule.h"
#include "esp_sntp.h"
#include "i2cdev.h"
#include "ds3231.h"
#include "math.h"
#include "app_common.h"

#define TAG     "APP_SCHEDULE"

#define NTP_SERVER 		"pool.ntp.org"
#define	TIMEZONE_OFFSET (7 * 60 * 60)

typedef struct {
    app_schedule_schedule_type_t type;
    int index;
    int start_hour;
    int start_minute;
    int start_sec;
    bool enable;
} app_schedule_job_t;

typedef struct app_schedule_ {
    app_callback_t* callback;
    app_schedule_job_t* job_list;
    int sda_pin;
    int scl_pin;
    app_network_handle_t network_handle;
    bool is_sync;
    bool is_init_sntp;
    i2c_dev_t dev;
    int risk_offset;
    bool is_force_sync;
    int n_sync_time;
    app_schedule_job_t start;
    app_schedule_job_t end;
    operator_mode_t mode;
} app_schedule_t;

app_schedule_handle_t app_schedule_init(app_schedule_config_t* config) {
    app_schedule_handle_t handle = malloc(sizeof(app_schedule_t));
    handle->callback = config->callback;
    handle->job_list = malloc(SCHEDULE_TYPE_MAX * sizeof(app_schedule_job_t));
    for (int i = 0; i < SCHEDULE_TYPE_MAX; i++) {
        handle->job_list[i].enable = false;
    }
    handle->scl_pin = config->scl_pin;
    handle->sda_pin = config->sda_pin;
    handle->network_handle = config->network_handle;
    handle->is_sync = false;
    handle->is_init_sntp = false;
    handle->is_force_sync = true;
    handle->risk_offset = config->risk_offset;
    handle->n_sync_time = 0;
    handle->mode = OPERATOR_MODE_OFF;
    return handle;
}

void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");
}

void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    //sntp_setservername(0, "pool.ntp.org");
    ESP_LOGI(TAG, "Your NTP Server is %s", NTP_SERVER);
    sntp_setservername(0, NTP_SERVER);
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    sntp_init();
}

void set_timezone_by_utc(int utc_offset)
{
    char tz[16];
    sprintf(tz, "GMT%c%d", utc_offset < 0 ? '+' : '-', abs(utc_offset));
    setenv("TZ", tz, 1);
    tzset();
}

esp_err_t _sync_time(app_schedule_handle_t handle) {

    if (handle->is_sync && !handle->is_force_sync) {
        return ESP_OK;
    }

    time_t now;
    struct tm timeinfo;
    char strftime_buf[64];

    if (!handle->is_force_sync) {

        time(&now);
        localtime_r(&now, &timeinfo);
        // timeinfo.tm_gmtoff = TIMEZONE_OFFSET;
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TAG, "The current date/time system is: %s", strftime_buf);

        if (timeinfo.tm_year > (2021 - 1900)) {
            ESP_LOGD(TAG, "valid time in system, no need to sync");
            handle->is_sync = true;
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            return ESP_OK;
        }

        // Get RTC date and time
        struct tm rtcinfo;

        if (ds3231_get_time(&handle->dev, &rtcinfo) != ESP_OK) {
            ESP_LOGE(TAG, "Could not get time.");
            return ESP_FAIL;
        }

        strftime(strftime_buf, sizeof(strftime_buf), "%c", &rtcinfo);
        ESP_LOGI(TAG, "The rtc date/time is: %s", strftime_buf);

        // ESP_LOGI(TAG, "rtc time %04d-%02d-%02d %02d:%02d:%02d", 
        //     rtcinfo.tm_year, rtcinfo.tm_mon + 1,
        //     rtcinfo.tm_mday, rtcinfo.tm_hour, rtcinfo.tm_min, rtcinfo.tm_sec);

        if (rtcinfo.tm_year > (2021 - 1900)) {
            ESP_LOGI(TAG, "rtc module has a valid time, to be sync with system");
            struct timeval set_time = { .tv_sec = mktime(&rtcinfo) };
            settimeofday(&set_time, NULL);
            handle->is_sync = true;
            return ESP_OK;
        } else {
            ESP_LOGI(TAG, "invalid rtc time, need to sync with sntp");
        }
    }

    if (!app_network_is_connected(handle->network_handle)) {
        vTaskDelay(10000 / portTICK_PERIOD_MS);
        handle->n_sync_time++;
        if (handle->n_sync_time > 6) {
            ESP_LOGE(TAG, "could not sync");
            handle->n_sync_time = 0;
            handle->is_force_sync = false;
        }
        return ESP_FAIL;
    }
    if (!handle->is_init_sntp) {
        initialize_sntp();
        handle->is_init_sntp = true;
    }
    handle->n_sync_time = 0;
    while (!handle->is_sync) {
        if (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET) {
            handle->n_sync_time++;
            if (handle->n_sync_time > 60) {
                handle->n_sync_time = 0;
                return ESP_FAIL;
            }
            ESP_LOGI(TAG, "Waiting for system time to be set... ");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "sync time complete");
        handle->is_sync = true;
        handle->is_init_sntp = false;
        sntp_stop();
        handle->is_force_sync = false;

        // update 'now' variable with current time
        time(&now);
        localtime_r(&now, &timeinfo);
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TAG, "The date/time after sync with sntp is: %s", strftime_buf);

        ESP_LOGD(TAG, "timeinfo.tm_sec=%d", timeinfo.tm_sec);
        ESP_LOGD(TAG, "timeinfo.tm_min=%d", timeinfo.tm_min);
        ESP_LOGD(TAG, "timeinfo.tm_hour=%d", timeinfo.tm_hour);
        ESP_LOGD(TAG, "timeinfo.tm_wday=%d", timeinfo.tm_wday);
        ESP_LOGD(TAG, "timeinfo.tm_mday=%d", timeinfo.tm_mday);
        ESP_LOGD(TAG, "timeinfo.tm_mon=%d", timeinfo.tm_mon);
        ESP_LOGD(TAG, "timeinfo.tm_year=%d", timeinfo.tm_year);

        struct tm time = {
            .tm_year = timeinfo.tm_year,
            .tm_mon  = timeinfo.tm_mon,  // 0-based
            .tm_mday = timeinfo.tm_mday,
            .tm_hour = timeinfo.tm_hour,
            .tm_min  = timeinfo.tm_min,
            .tm_sec  = timeinfo.tm_sec
        };

        if (ds3231_set_time(&handle->dev, &time) != ESP_OK) {
            ESP_LOGE(TAG, "Could not set time.");
        }
    }
    return ESP_OK;
}

void _schedule_task(void* pv) {
    app_schedule_handle_t handle = (app_schedule_handle_t)pv;

    set_timezone_by_utc(7);

    if (ds3231_init_desc(&handle->dev, I2C_NUM_0, handle->sda_pin, handle->scl_pin) != ESP_OK) {
        ESP_LOGE(TAG, "Could not init device descriptor.");
        while (1) { vTaskDelay(100); }
    }

    app_schedule_schedule_type_t type = SCHEDULE_TYPE_NONE;

    // Get RTC date and time
    struct tm rtcinfo;

    if (ds3231_get_time(&handle->dev, &rtcinfo) != ESP_OK) {
        ESP_LOGE(TAG, "Could not get time.");
        return ESP_FAIL;
    }

    if (rtcinfo.tm_year > (2021 - 1900)) {
        ESP_LOGI(TAG, "rtc module has a valid time, to be sync with system");
        struct timeval set_time = { .tv_sec = mktime(&rtcinfo) };
        settimeofday(&set_time, NULL);
    } else {
        ESP_LOGI(TAG, "invalid rtc time, need to sync with sntp");
    }

    while (1) {
        AWAIT(_sync_time(handle));
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        // timeinfo.tm_gmtoff = TIMEZONE_OFFSET;
        char strftime_buf[64];
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        // ESP_LOGD(TAG, "The current date/time system is: %s", strftime_buf);

        if (timeinfo.tm_year < (2021 - 1900)) {
            ESP_LOGD(TAG, "invalid time in system, need to sync");
            handle->is_sync = false;
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        // start = NULL;
        // end = NULL;

        // for (int i = 0; i < SCHEDULE_TYPE_MAX; i++) {
        //     if (handle->job_list[i].enable) {
        //         if (handle->job_list[i].type == SCHEDULE_TYPE_ACTIVE_START) {
        //             start = &(handle->job_list[i]);
        //         } else if (handle->job_list[i].type == SCHEDULE_TYPE_ACTIVE_END) {
        //             end = &(handle->job_list[i]);
        //         }
                // app_schedule_job_t* job = &(handle->job_list[i]);

                // if (job->start_hour == timeinfo.tm_hour &&
                //     job->start_minute == timeinfo.tm_min &&
                //     (job->start_sec + handle->risk_offset) >= timeinfo.tm_sec) {
                //     ESP_LOGD(TAG, "The current date/time system is: %s", strftime_buf);
                //     ESP_LOGI(TAG, "execute schedule %d", (int)job->type);
                //     if (handle->callback != NULL) {
                //         handle->callback(APP_SCHEDULE_EVENT_TRIGGER, &job->type, 1);
                //     }
                // }
        //     }
        // }
        if (handle->mode != OPERATOR_MODE_AUTO) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        if (handle->callback == NULL) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        if (!handle->start.enable || !handle->end.enable) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        int c_start_time = handle->start.start_hour * 60 + handle->start.start_minute;
        int c_end_time = handle->end.start_hour * 60 + handle->end.start_minute;
        int c_check_time = timeinfo.tm_hour * 60 + timeinfo.tm_min;
        if (c_start_time <= c_end_time) {
            if (c_check_time >= c_start_time && c_check_time <= c_end_time) {
                type = SCHEDULE_TYPE_ACTIVE_START;
                handle->callback(APP_SCHEDULE_EVENT_TRIGGER, &type, sizeof(app_schedule_schedule_type_t));
            } else {
                type = SCHEDULE_TYPE_ACTIVE_END;
                handle->callback(APP_SCHEDULE_EVENT_TRIGGER, &type, sizeof(app_schedule_schedule_type_t));
            }
        } else {
            if (c_check_time >= c_start_time || c_check_time <= c_end_time) {
                type = SCHEDULE_TYPE_ACTIVE_START;
                handle->callback(APP_SCHEDULE_EVENT_TRIGGER, &type, sizeof(app_schedule_schedule_type_t));
            } else {
                type = SCHEDULE_TYPE_ACTIVE_END;
                handle->callback(APP_SCHEDULE_EVENT_TRIGGER, &type, sizeof(app_schedule_schedule_type_t));
            }
        }

        if ((c_check_time * 60 + timeinfo.tm_sec) >= ((RESTART_HOUR * 60 + RESTAR_MINUTE) * 60) &&
            (c_check_time * 60 + timeinfo.tm_sec) <= ((RESTART_HOUR * 60 + RESTAR_MINUTE) * 60 + 2)) {
            ESP_LOGD(TAG, "to be reset");
            esp_restart();
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

esp_err_t app_schedule_start(app_schedule_handle_t handle) {
    if (handle == NULL) {
        return ESP_FAIL;
    }

    if (xTaskCreate(_schedule_task, "_schedule_task", 1024 * 4, handle, 5, NULL) != pdTRUE) {
        ESP_LOGE(TAG, "Error create console task, memory exhausted?");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t app_schedule_add_alarm(app_schedule_handle_t handle, int hour, int minute, app_schedule_schedule_type_t type) {
    if (handle == NULL) {
        return ESP_FAIL;
    }
    int i = (int)type;
    handle->job_list[i].type = type;
    handle->job_list[i].enable = true;
    handle->job_list[i].start_hour = hour;
    handle->job_list[i].start_minute = minute;
    handle->job_list[i].start_sec = 0;
    if (type == SCHEDULE_TYPE_ACTIVE_START) {
        handle->start.start_hour = hour;
        handle->start.start_minute = minute;
        handle->start.enable = true;
        handle->start.type = SCHEDULE_TYPE_ACTIVE_START;
    } else if (type == SCHEDULE_TYPE_ACTIVE_END) {
        handle->end.start_hour = hour;
        handle->end.start_minute = minute;
        handle->end.enable = true;
        handle->end.type = SCHEDULE_TYPE_ACTIVE_END;
    }
    return ESP_OK;
}

esp_err_t app_schedule_set_time(app_schedule_handle_t handle, date_time_form_t time_form) {
    if (handle == NULL) {
        return ESP_FAIL;
    }
    struct tm time = {
        .tm_year = time_form.year - 1900,
        .tm_mon  = time_form.month - 1,  // 0-based
        .tm_mday = time_form.day,
        .tm_hour = time_form.hour,
        .tm_min  = time_form.minute,
        .tm_sec  = 0
    };

    if (ds3231_set_time(&handle->dev, &time) != ESP_OK) {
        ESP_LOGE(TAG, "Could not set time.");
        return ESP_FAIL;
    }
    struct timeval set_time = { .tv_sec = mktime(&time) };
    settimeofday(&set_time, NULL);
    ESP_LOGE(TAG, "set time success");
    handle->is_sync = false;
    return ESP_OK;
}

esp_err_t app_schedule_sync_time(app_schedule_handle_t handle) {
    if (handle == NULL) {
        return ESP_FAIL;
    }
    handle->is_force_sync = true;
    handle->n_sync_time = 0;
    handle->is_sync = false;
    return ESP_OK;
}

esp_err_t app_schedule_set_mode(app_schedule_handle_t handle, operator_mode_t mode) {
    if (handle == NULL) {
        return ESP_FAIL;
    }
    handle->mode = mode;
    return ESP_OK;
}