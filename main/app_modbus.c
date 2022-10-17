#include "app_modbus.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "modbus_params.h"  // for modbus parameters structures
#include "mbcontroller.h"
#include "sdkconfig.h"

#define TAG "APP_MODBUS"

// #define SIMULATE

// The number of parameters that intended to be used in the particular control process
#define MASTER_MAX_CIDS num_device_parameters

// Number of reading of parameters from slave
#define MASTER_MAX_RETRY 30

// Timeout to update cid over Modbus
#define UPDATE_CIDS_TIMEOUT_MS          (500)
#define UPDATE_CIDS_TIMEOUT_TICS        (UPDATE_CIDS_TIMEOUT_MS / portTICK_RATE_MS)

// Timeout between polls
#define POLL_TIMEOUT_MS                 (5)
#define POLL_TIMEOUT_TICS               (POLL_TIMEOUT_MS / portTICK_RATE_MS)

#define MASTER_CHECK(a, ret_val, str, ...) \
    if (!(a)) { \
        ESP_LOGE(TAG, "%s(%u): " str, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
        return (ret_val); \
    }

// The macro to get offset for parameter in the appropriate structure
#define HOLD_OFFSET(field) ((uint16_t)(offsetof(holding_reg_params_t, field) + 1))
#define INPUT_OFFSET(field) ((uint16_t)(offsetof(input_reg_params_t, field) + 1))
#define COIL_OFFSET(field) ((uint16_t)(offsetof(coil_reg_params_t, field) + 1))
// Discrete offset macro
#define DISCR_OFFSET(field) ((uint16_t)(offsetof(discrete_reg_params_t, field) + 1))

#define STR(fieldname) ((const char*)( fieldname ))
// Options can be used as bit masks or parameter limits
#define OPTS(min_val, max_val, step_val) { .opt1 = min_val, .opt2 = max_val, .opt3 = step_val }

// Enumeration of modbus device addresses accessed by master device
enum {
    MB_DEVICE_ADDR1 = 2 // Only one slave device used for the test (add other slave addresses here)
};

// Enumeration of all supported CIDs for device (used in parameter definition table)
enum {
    CID_INP_DATA_0 = 0,
    CID_HOLD_DATA_0,
    CID_INP_DATA_1,
    CID_HOLD_DATA_1,
    CID_INP_DATA_2,
    CID_HOLD_DATA_2,
    CID_RELAY_P1,
    CID_RELAY_P2,
    CID_COUNT
};

// Example Data (Object) Dictionary for Modbus parameters:
// The CID field in the table must be unique.
// Modbus Slave Addr field defines slave address of the device with correspond parameter.
// Modbus Reg Type - Type of Modbus register area (Holding register, Input Register and such).
// Reg Start field defines the start Modbus register number and Reg Size defines the number of registers for the characteristic accordingly.
// The Instance Offset defines offset in the appropriate parameter structure that will be used as instance to save parameter value.
// Data Type, Data Size specify type of the characteristic and its data size.
// Parameter Options field specifies the options that can be used to process parameter value (limits or masks).
// Access Mode - can be used to implement custom options for processing of characteristic (Read/Write restrictions, factory mode values and etc).
const mb_parameter_descriptor_t device_parameters[] = {
    { 0, STR("V1"), STR("Volts"), MB_DEVICE_ADDR1, MB_PARAM_INPUT, 0, 2,
                    INPUT_OFFSET(input_data0), PARAM_TYPE_FLOAT, 4, OPTS( -10, 10, 1 ), PAR_PERMS_READ_WRITE_TRIGGER },
    { 1, STR("V2"), STR("Volts"), MB_DEVICE_ADDR1, MB_PARAM_INPUT, 2, 2,
                    INPUT_OFFSET(input_data0), PARAM_TYPE_FLOAT, 4, OPTS( -10, 10, 1 ), PAR_PERMS_READ_WRITE_TRIGGER },
    { 2, STR("V3"), STR("Volts"), MB_DEVICE_ADDR1, MB_PARAM_INPUT, 4, 2,
                    INPUT_OFFSET(input_data0), PARAM_TYPE_FLOAT, 4, OPTS( -10, 10, 1 ), PAR_PERMS_READ_WRITE_TRIGGER },
    { 3, STR("I1"), STR("Ampe"), MB_DEVICE_ADDR1, MB_PARAM_INPUT, 16, 2,
                    INPUT_OFFSET(input_data0), PARAM_TYPE_FLOAT, 4, OPTS( -10, 10, 1 ), PAR_PERMS_READ_WRITE_TRIGGER },
    { 4, STR("I2"), STR("Ampe"), MB_DEVICE_ADDR1, MB_PARAM_INPUT, 18, 2,
                    INPUT_OFFSET(input_data0), PARAM_TYPE_FLOAT, 4, OPTS( -10, 10, 1 ), PAR_PERMS_READ_WRITE_TRIGGER },
    { 5, STR("I3"), STR("Ampe"), MB_DEVICE_ADDR1, MB_PARAM_INPUT, 20, 2,
                    INPUT_OFFSET(input_data0), PARAM_TYPE_FLOAT, 4, OPTS( -10, 10, 1 ), PAR_PERMS_READ_WRITE_TRIGGER },
    { 6, STR("P1"), STR("KWatt"), MB_DEVICE_ADDR1, MB_PARAM_INPUT, 24, 2,
                    INPUT_OFFSET(input_data0), PARAM_TYPE_FLOAT, 4, OPTS( -10, 10, 1 ), PAR_PERMS_READ_WRITE_TRIGGER },
    { 7, STR("P2"), STR("KWatt"), MB_DEVICE_ADDR1, MB_PARAM_INPUT, 26, 2,
                    INPUT_OFFSET(input_data0), PARAM_TYPE_FLOAT, 4, OPTS( -10, 10, 1 ), PAR_PERMS_READ_WRITE_TRIGGER },
    { 8, STR("P3"), STR("KWatt"), MB_DEVICE_ADDR1, MB_PARAM_INPUT, 28, 2,
                    INPUT_OFFSET(input_data0), PARAM_TYPE_FLOAT, 4, OPTS( -10, 10, 1 ), PAR_PERMS_READ_WRITE_TRIGGER },
    { 9, STR("PF1"), STR("cosW"), MB_DEVICE_ADDR1, MB_PARAM_INPUT, 48, 2,
                    INPUT_OFFSET(input_data0), PARAM_TYPE_FLOAT, 4, OPTS( -10, 10, 1 ), PAR_PERMS_READ_WRITE_TRIGGER },
    { 10, STR("PF2"), STR("cosW"), MB_DEVICE_ADDR1, MB_PARAM_INPUT, 50, 2,
                    INPUT_OFFSET(input_data0), PARAM_TYPE_FLOAT, 4, OPTS( -10, 10, 1 ), PAR_PERMS_READ_WRITE_TRIGGER },
    { 11, STR("PF3"), STR("cosW"), MB_DEVICE_ADDR1, MB_PARAM_INPUT, 52, 2,
                    INPUT_OFFSET(input_data0), PARAM_TYPE_FLOAT, 4, OPTS( -10, 10, 1 ), PAR_PERMS_READ_WRITE_TRIGGER },
    // { 12, STR("F1"), STR("Hz"), MB_DEVICE_ADDR1, MB_PARAM_INPUT, 56, 2,
    //                 INPUT_OFFSET(input_data0), PARAM_TYPE_FLOAT, 4, OPTS( -10, 10, 1 ), PAR_PERMS_READ_WRITE_TRIGGER },
    // { 13, STR("F2"), STR("Hz"), MB_DEVICE_ADDR1, MB_PARAM_INPUT, 58, 2,
    //                 INPUT_OFFSET(input_data0), PARAM_TYPE_FLOAT, 4, OPTS( -10, 10, 1 ), PAR_PERMS_READ_WRITE_TRIGGER },
    // { 14, STR("F3"), STR("Hz"), MB_DEVICE_ADDR1, MB_PARAM_INPUT, 60, 2,
    //                 INPUT_OFFSET(input_data0), PARAM_TYPE_FLOAT, 4, OPTS( -10, 10, 1 ), PAR_PERMS_READ_WRITE_TRIGGER },
    { 12, STR("Power"), STR("KWh"), MB_DEVICE_ADDR1, MB_PARAM_INPUT, 58, 2,
                    INPUT_OFFSET(input_data0), PARAM_TYPE_FLOAT, 4, OPTS( -10, 10, 1 ), PAR_PERMS_READ_WRITE_TRIGGER },

};

// Calculate number of parameters in the table
const uint16_t num_device_parameters = (sizeof(device_parameters)/sizeof(device_parameters[0]));

typedef struct app_modbus_ {
    bool running;
    int update_interval_ms;
    uint8_t port_num;
    int tx_pin;
    int rx_pin;
    int rst_pin;
    int baudrate;
    app_callback_t* callback;
    phases_data_t phase_data;
} app_modbus_t;

// The function to get pointer to parameter storage (instance) according to parameter description table
void* master_get_param_data(const mb_parameter_descriptor_t* param_descriptor)
{
    assert(param_descriptor != NULL);
    void* instance_ptr = NULL;
    if (param_descriptor->param_offset != 0) {
       switch(param_descriptor->mb_param_type)
       {
           case MB_PARAM_HOLDING:
               instance_ptr = ((void*)&holding_reg_params + param_descriptor->param_offset - 1);
               break;
           case MB_PARAM_INPUT:
               instance_ptr = ((void*)&input_reg_params + param_descriptor->param_offset - 1);
               break;
           case MB_PARAM_COIL:
               instance_ptr = ((void*)&coil_reg_params + param_descriptor->param_offset - 1);
               break;
           case MB_PARAM_DISCRETE:
               instance_ptr = ((void*)&discrete_reg_params + param_descriptor->param_offset - 1);
               break;
           default:
               instance_ptr = NULL;
               break;
       }
    } else {
        ESP_LOGE(TAG, "Wrong parameter offset for CID #%d", param_descriptor->cid);
        assert(instance_ptr != NULL);
    }
    return instance_ptr;
}

esp_err_t _modbus_init(app_modbus_handle_t handle) {
    // Initialize and start Modbus controller
    mb_communication_info_t comm = {
        .port = handle->port_num,
        .mode = MB_MODE_RTU,
        .baudrate = handle->baudrate,
        .parity = MB_PARITY_NONE
    };
    void* master_handler = NULL;

    esp_err_t err = mbc_master_init(MB_PORT_SERIAL_MASTER, &master_handler);
    MASTER_CHECK((master_handler != NULL), ESP_ERR_INVALID_STATE,
                                "mb controller initialization fail.");
    MASTER_CHECK((err == ESP_OK), ESP_ERR_INVALID_STATE,
                            "mb controller initialization fail, returns(0x%x).",
                            (uint32_t)err);
    err = mbc_master_setup((void*)&comm);
    MASTER_CHECK((err == ESP_OK), ESP_ERR_INVALID_STATE,
                            "mb controller setup fail, returns(0x%x).",
                            (uint32_t)err);

    // Set UART pin numbers
    err = uart_set_pin(handle->port_num, handle->tx_pin, handle->rx_pin, handle->rst_pin, UART_PIN_NO_CHANGE);

    err = mbc_master_start();
    MASTER_CHECK((err == ESP_OK), ESP_ERR_INVALID_STATE,
                            "mb controller start fail, returns(0x%x).",
                            (uint32_t)err);

    MASTER_CHECK((err == ESP_OK), ESP_ERR_INVALID_STATE,
            "mb serial set pin failure, uart_set_pin() returned (0x%x).", (uint32_t)err);
    // Set driver mode to Half Duplex
    err = uart_set_mode(handle->port_num, UART_MODE_RS485_HALF_DUPLEX);
    MASTER_CHECK((err == ESP_OK), ESP_ERR_INVALID_STATE,
            "mb serial set mode failure, uart_set_mode() returned (0x%x).", (uint32_t)err);
    vTaskDelay(5);
    err = mbc_master_set_descriptor(&device_parameters[0], num_device_parameters);
    MASTER_CHECK((err == ESP_OK), ESP_ERR_INVALID_STATE,
                                "mb controller set descriptor fail, returns(0x%x).",
                                (uint32_t)err);
    ESP_LOGI(TAG, "Modbus master stack initialized...");

    return ESP_OK;

}

static void _callback(app_modbus_handle_t handle, int event_id, void* data, int len) {
    if (handle->callback != NULL) {
        handle->callback(event_id, data, len);
    }
}

void _set_phases_data(int cid, float data, phases_data_t* phase_data) {
    switch (cid)
    {
    case 0:
        phase_data->u1 = data;
        break;
    case 1:
        phase_data->u2 = data;
        break;
    case 2:
        phase_data->u3 = data;
        break;
    case 3:
        phase_data->i1 = data;
        break;
    case 4:
        phase_data->i2 = data;
        break;
    case 5:
        phase_data->i3 = data;
        break;
    case 6:
        phase_data->p1 = data;
        break;
    case 7:
        phase_data->p2 = data;
        break;
    case 8:
        phase_data->p3 = data;
        break;
    case 9:
        phase_data->phy1 = data;
        break;
    case 10:
        phase_data->phy2 = data;
        break;
    case 11:
        phase_data->phy3 = data;
        break;
    case 12:
        phase_data->power = data;
    default:
        break;
    }
}

void _modbus_task(void* pv) {
    app_modbus_handle_t handle = (app_modbus_handle_t)pv;

#if defined SIMULATE

    while (1)
    {
        handle->phase_data.u1 = (++handle->phase_data.u1) > 25 ? 0 : handle->phase_data.u1;
        handle->phase_data.u2 = (++handle->phase_data.u2) > 50 ? 0 : handle->phase_data.u2;
        handle->phase_data.u3 = (++handle->phase_data.u3) > 220 ? 0 : handle->phase_data.u3;

        handle->phase_data.i1 = (++handle->phase_data.i1) > 2 ? 0 : handle->phase_data.i1;
        handle->phase_data.i2 = (++handle->phase_data.i2) > 5 ? 0 : handle->phase_data.i2;
        handle->phase_data.i3 = (++handle->phase_data.i3) > 10 ? 0 : handle->phase_data.i3;

        handle->phase_data.p1 = (++handle->phase_data.p1) > 20 ? 0 : handle->phase_data.p1;
        handle->phase_data.p2 = (++handle->phase_data.p2) > 30 ? 0 : handle->phase_data.p2;
        handle->phase_data.p3 = (++handle->phase_data.p3) > 1000 ? 0 : handle->phase_data.p3;

        handle->phase_data.power = 10;

        _callback(handle, APP_MODBUS_EVENT_DATA, &handle->phase_data, sizeof(phases_data_t));
        vTaskDelay(handle->update_interval_ms / portTICK_RATE_MS);
    }
    

#else

    ESP_LOGI(TAG, "To be connect modbus");

    while (_modbus_init(handle) != ESP_OK) {
        ESP_LOGI(TAG, "Modbus master init failed... retrying...");
        vTaskDelay(5000 / portTICK_RATE_MS);
    }

    ESP_LOGI(TAG, "Modbus master init success");
    vTaskDelay(500 / portTICK_RATE_MS);

    esp_err_t err = ESP_OK;
    float value = 0;
    bool alarm_state = false;
    const mb_parameter_descriptor_t* param_descriptor = NULL;

    ESP_LOGI(TAG, "Start modbus...");

    while (handle->running) {
        // Read all found characteristics from slave(s)
        for (uint16_t cid = 0; (err != ESP_ERR_NOT_FOUND) && cid < MASTER_MAX_CIDS; cid++)
        {
            // Get data from parameters description table
            // and use this information to fill the characteristics description table
            // and having all required fields in just one table
            bool pass = false;
            for (int i = 0; i < 10; i++) {
                if (pass) {
                    break;
                }
                err = mbc_master_get_cid_info(cid, &param_descriptor);
                if ((err != ESP_ERR_NOT_FOUND) && (param_descriptor != NULL)) {
                    void* temp_data_ptr = master_get_param_data(param_descriptor);
                    if (temp_data_ptr == NULL) {
                        continue;
                    }
                    uint8_t type = 0;
                    err = mbc_master_get_parameter(cid, (char*)param_descriptor->param_key,
                                                        (uint8_t*)&value, &type);
                    if (err == ESP_OK) {
                        *(float*)temp_data_ptr = value;
                        if ((param_descriptor->mb_param_type == MB_PARAM_HOLDING) ||
                            (param_descriptor->mb_param_type == MB_PARAM_INPUT)) {
                            ESP_LOGI(TAG, "Characteristic #%d %s (%s) value = %f (0x%x) read successful.",
                                            param_descriptor->cid,
                                            (char*)param_descriptor->param_key,
                                            (char*)param_descriptor->param_units,
                                            value,
                                            *(uint32_t*)temp_data_ptr);
                            _set_phases_data(param_descriptor->cid, value, &handle->phase_data);
                            pass = true;
                        } else {
                            uint16_t state = *(uint16_t*)temp_data_ptr;
                            const char* rw_str = (state & param_descriptor->param_opts.opt1) ? "ON" : "OFF";
                            ESP_LOGI(TAG, "Characteristic #%d %s (%s) value = %s (0x%x) read successful.",
                                            param_descriptor->cid,
                                            (char*)param_descriptor->param_key,
                                            (char*)param_descriptor->param_units,
                                            (const char*)rw_str,
                                            *(uint16_t*)temp_data_ptr);
                        }
                    } else {
                        ESP_LOGE(TAG, "Characteristic #%d (%s) read fail, err = %d (%s).",
                                            param_descriptor->cid,
                                            (char*)param_descriptor->param_key,
                                            (int)err,
                                            (char*)esp_err_to_name(err));
                    }
                    vTaskDelay(POLL_TIMEOUT_TICS); // timeout between polls
                }
            }
        }
        _callback(handle, APP_MODBUS_EVENT_DATA, &handle->phase_data, sizeof(phases_data_t));
        vTaskDelay(handle->update_interval_ms / portTICK_RATE_MS);
    } 
    ESP_LOGI(TAG, "Destroy master...");
    ESP_ERROR_CHECK(mbc_master_destroy());
#endif
}

app_modbus_handle_t app_modbus_init(app_modbus_config_t* config) {
    if (config == NULL) {
        return NULL;
    }
    app_modbus_handle_t handle = malloc(sizeof(app_modbus_t));
    handle->running = false;
    handle->port_num = config->port_num;
    handle->baudrate = config->baudrate;
    handle->tx_pin = config->tx_pin;
    handle->rx_pin = config->rx_pin;
    handle->rst_pin = config->rst_pin;
    handle->update_interval_ms = config->update_interval_ms;
    handle->callback = config->callback;
    memset(&handle->phase_data, 0, sizeof(phases_data_t));

    return handle;
}

esp_err_t app_modbus_start(app_modbus_handle_t handle) {
    handle->running = true;
    if (xTaskCreate(_modbus_task, "_modbus_task", 1024 * 4, handle, 5, NULL) != pdTRUE) {
        ESP_LOGE(TAG, "Error create console task, memory exhausted?");
        return ESP_FAIL;
    }
    return ESP_OK;
}

