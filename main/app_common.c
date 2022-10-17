#include "app_common.h"
#include "esp_timer.h"

long long esp_tick_get()
{
    return esp_timer_get_time()/1000;
}