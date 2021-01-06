#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/periph_ctrl.h"
#include "driver/timer.h"
#include "freertos/task.h"


#define GPIO_OUTPUT_IO_0    2
#define GPIO_OUTPUT_PIN_SEL  (1ULL<<GPIO_OUTPUT_IO_0)
#define GPIO_INPUT_IO_0     4
#define GPIO_INPUT_PIN_SEL  ((1ULL<<GPIO_INPUT_IO_0) | (1ULL<<15) | (1ULL<<16) | (1ULL<<17) | (1ULL<<18) | (1ULL<<19))
#define ESP_INTR_FLAG_DEFAULT 0
#define INCLUDE_xTaskResumeFromISR 1
#define INCLUDE_vTaskSuspend 1

const int f = 1600;
const int base_speed = 320;
int speed = 320;
uint32_t timer_divider = (TIMER_BASE_CLK / f );
TaskHandle_t task = NULL;

static void IRAM_ATTR gpio_isr_handler(void* arg){
    uint32_t gpio_num = (uint32_t) arg;
    if(gpio_num == GPIO_INPUT_IO_0){
        gpio_set_level(GPIO_OUTPUT_IO_0, 1);
        timer_start(TIMER_GROUP_0, TIMER_0);
        timer_set_alarm(TIMER_GROUP_0, TIMER_0, TIMER_ALARM_EN);
        return;
    }
}

void IRAM_ATTR timer_group0_isr(){
    timer_spinlock_take(TIMER_GROUP_0);

    uint32_t timer_intr = timer_group_get_intr_status_in_isr(TIMER_GROUP_0);
    if (timer_intr & TIMER_INTR_T0) {
        gpio_set_level(GPIO_OUTPUT_IO_0, 0);
        timer_group_clr_intr_status_in_isr(TIMER_GROUP_0, TIMER_0);
        timer_pause(TIMER_GROUP_0, TIMER_0);
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

    /* Timer's counter will initially start from value below.
       Also, if auto_reload is set, this value will be automatically reload on alarm */
    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0x00000000ULL);

    /* Configure the alarm value and the interrupt on alarm. */
    timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, speed);
    timer_enable_intr(TIMER_GROUP_0, TIMER_0);
    timer_isr_register(TIMER_GROUP_0, TIMER_0, timer_group0_isr, NULL, ESP_INTR_FLAG_IRAM, NULL);

    //install gpio isr service
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    //hook isr handler for specific gpio pin
    gpio_isr_handler_add(GPIO_INPUT_IO_0, gpio_isr_handler, (void*) GPIO_INPUT_IO_0);
    
    while(1){
        int pin[] = {15, 16, 17, 18, 19};
        for(int i = 0; i<5; i++){
            int level = gpio_get_level(pin[i]);
            if(!level) speed = base_speed * (pin[i] - 15);
        }
        printf("Current speed: %d \n", speed / base_speed);
        timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, speed);
        vTaskDelay(5000/portTICK_PERIOD_MS);
    }

}
