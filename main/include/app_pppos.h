#ifndef _APP_PPP_H_
#define _APP_PPP_H_

typedef struct app_ppp_* app_ppp_handle_t;

// typedef struct {
//     int tx_pin;
//     int rx_pin;
//     int reset_pin;
//     int ri_pin;
//     int dtr_pin;
//     int uart_num;
// } app_ppp_modem_conn_t;

typedef struct {
} app_ppp_cfg_t;

app_ppp_handle_t app_ppp_init(app_ppp_cfg_t* ppp_cfg);

bool app_ppp_is_connected(app_ppp_handle_t periph);

void app_ppp_get_ip_address(app_ppp_handle_t periph);


#endif