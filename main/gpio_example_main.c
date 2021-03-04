#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/periph_ctrl.h"
#include "driver/timer.h"
#include "freertos/task.h"


#define GPIO_OUTPUT_IO_0    5
#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<GPIO_OUTPUT_IO_0) | (1ULL<<4) | (1ULL<<16) | (1ULL<<17))
#define GPIO_INPUT_IO_0     2
#define GPIO_INPUT_PIN_SEL  (1ULL<<GPIO_INPUT_IO_0)
#define ESP_INTR_FLAG_DEFAULT 0
#define INCLUDE_xTaskResumeFromISR 1
#define INCLUDE_vTaskSuspend 1

const int f = 10000;
const int max_speed = 100;
const int min_speed = 0;
const int inc = 15;
int speed = 0;
uint32_t timer_divider = (TIMER_BASE_CLK / f );
TaskHandle_t task = NULL;
int arr[8] = { 0 };

static void IRAM_ATTR gpio_isr_handler(void* arg){
    uint32_t gpio_num = (uint32_t) arg;
    if(gpio_num == GPIO_INPUT_IO_0 && speed == min_speed){
        gpio_set_level(GPIO_OUTPUT_IO_0, 1);
        timer_set_alarm(TIMER_GROUP_0, TIMER_1, TIMER_ALARM_EN);
        timer_start(TIMER_GROUP_0, TIMER_1);
    }
    else if(gpio_num == GPIO_INPUT_IO_0 && speed < max_speed){
        timer_set_alarm(TIMER_GROUP_0, TIMER_0, TIMER_ALARM_EN);
        timer_start(TIMER_GROUP_0, TIMER_0);
        return;
    }
}

void IRAM_ATTR timer_group0_isr(){
    timer_spinlock_take(TIMER_GROUP_0);

    uint32_t timer_intr = timer_group_get_intr_status_in_isr(TIMER_GROUP_0);
    if (timer_intr & TIMER_INTR_T0) {
        gpio_set_level(GPIO_OUTPUT_IO_0, 1);
        
        // start turn off alaram
        timer_set_alarm(TIMER_GROUP_0, TIMER_1, TIMER_ALARM_EN);
        timer_start(TIMER_GROUP_0, TIMER_1);

        // pause turn on alaram
        timer_group_clr_intr_status_in_isr(TIMER_GROUP_0, TIMER_0);
        timer_pause(TIMER_GROUP_0, TIMER_0);
    } else if (timer_intr & TIMER_INTR_T1) {
        gpio_set_level(GPIO_OUTPUT_IO_0, 0);

        // pause turn off alaram
        timer_group_clr_intr_status_in_isr(TIMER_GROUP_0, TIMER_1);
        timer_pause(TIMER_GROUP_0, TIMER_1);
    } 

    timer_spinlock_give(TIMER_GROUP_0);
}

void app_main(void){

    gpio_config_t io_conf;
    //disable interrupt
    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //disable pull-up mode
    io_conf.pull_up_en = 0;
    //configure GPIO with the given settings
    gpio_config(&io_conf);

    //interrupt of rising edge
    io_conf.intr_type = GPIO_PIN_INTR_POSEDGE;
    //bit mask of the pins, use GPIO4/5 here
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    //set as input mode    
    io_conf.mode = GPIO_MODE_INPUT;
    //enable pull-up mode
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);

    //change gpio intrrupt type for one pin
    gpio_set_intr_type(GPIO_INPUT_IO_0, GPIO_INTR_POSEDGE);

    timer_config_t config = {
        .divider = timer_divider,
        .counter_dir = TIMER_COUNT_UP,
        .counter_en = TIMER_PAUSE,
        .alarm_en = TIMER_ALARM_DIS,
        .auto_reload = TIMER_AUTORELOAD_EN,
    };

    // default clock source is APB
    timer_init(TIMER_GROUP_0, TIMER_0, &config);
    timer_init(TIMER_GROUP_0, TIMER_1, &config);

    /* Timer's counter will initially start from value below.
       Also, if auto_reload is set, this value will be automatically reload on alarm */
    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0x00000000ULL);
    timer_set_counter_value(TIMER_GROUP_0, TIMER_1, 0x00000000ULL);

    /* Configure the alarm value and the interrupt on alarm. */
    timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, speed);
    timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, 5);

    timer_enable_intr(TIMER_GROUP_0, TIMER_0);
    timer_enable_intr(TIMER_GROUP_0, TIMER_1);

    timer_isr_register(TIMER_GROUP_0, TIMER_0, timer_group0_isr, NULL, ESP_INTR_FLAG_IRAM, NULL);
    timer_isr_register(TIMER_GROUP_0, TIMER_1, timer_group0_isr, NULL, ESP_INTR_FLAG_IRAM, NULL);

    //install gpio isr service
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    //hook isr handler for specific gpio pin
    gpio_isr_handler_add(GPIO_INPUT_IO_0, gpio_isr_handler, (void*) GPIO_INPUT_IO_0);
    
    while(1){
        int delay = 5000, _inc = inc;
        
        for( ; ; ){

            int _speed = speed/inc;
            
            printf("Current speed: %d \n", _speed);
            timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, speed);
            
            
            for(int j=0; j < 8; j++){
                arr[j] = 0;
            }

            arr[_speed] = 1;

            for(int j=7; j >= 0; j--){
                printf("led: %d : %d\n", j, arr[j]);
                gpio_set_level(4, arr[j]);
                gpio_set_level(17, 1);
                vTaskDelay(10/portTICK_PERIOD_MS);
                gpio_set_level(17, 0);
                gpio_set_level(4, 0);
                vTaskDelay(10/portTICK_PERIOD_MS);
            }
            
            gpio_set_level(16, 1);
            vTaskDelay(10/portTICK_PERIOD_MS);
            gpio_set_level(16, 0);
            
            vTaskDelay(delay/portTICK_PERIOD_MS);
            speed += _inc;

            if(speed >= max_speed) _inc = -1 * inc;
            if(speed <= min_speed) _inc = inc;
        }
    }

}
