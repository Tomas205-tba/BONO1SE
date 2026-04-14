#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/adc.h"

// ==========================
// DEFINICIÓN DE PINES
// ==========================

// Entradas (botones)
#define BTN_RIGHT   25
#define BTN_LEFT    33

// LEDs indicadores
#define LED_GREEN   15
#define LED_RED     14

// Segmentos display
#define SEG_A 23
#define SEG_B 22
#define SEG_C 21
#define SEG_D 19
#define SEG_E 18
#define SEG_F 5
#define SEG_G 17

// Dígitos (ánodo común)
#define DIG_HUNDREDS 16
#define DIG_TENS     4
#define DIG_UNITS    32

// Puente H
#define HS_L 12
#define LS_L 26
#define HS_R 13
#define LS_R 27

// ADC
#define ADC_CH ADC1_CHANNEL_6

// PWM
#define PWM_FREQ     5000
#define PWM_RES      LEDC_TIMER_8_BIT
#define PWM_SPEED    LEDC_HIGH_SPEED_MODE
#define PWM_TIMER_ID LEDC_TIMER_0
#define PWM_CH_L     LEDC_CHANNEL_0
#define PWM_CH_R     LEDC_CHANNEL_1
#define PWM_MAX      255

// Tiempos
#define T_SAFE   300
#define T_DISP   2
#define T_BTN    20

typedef enum {
    RIGHT = 0,
    LEFT
} dir_t;

// ==========================
// VARIABLES
// ==========================
static volatile int percent = 0;
static volatile int pwm_val = 0;
static volatile dir_t dir_current = RIGHT;
static volatile dir_t dir_request = RIGHT;

// ==========================
// TABLA DISPLAY
// ==========================
static const uint8_t map_digits[10][7] = {
    {0,0,0,0,0,0,1},
    {1,0,0,1,1,1,1},
    {0,0,1,0,0,1,0},
    {0,0,0,0,1,1,0},
    {1,0,0,1,1,0,0},
    {0,1,0,0,1,0,0},
    {0,1,0,0,0,0,0},
    {0,0,0,1,1,1,1},
    {0,0,0,0,0,0,0},
    {0,0,0,0,1,0,0}
};

// ==========================
// CONFIG GPIO
// ==========================
static void config_gpio(void)
{
    gpio_config_t outs = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask =
            (1ULL<<LED_GREEN) | (1ULL<<LED_RED) |
            (1ULL<<SEG_A) | (1ULL<<SEG_B) | (1ULL<<SEG_C) |
            (1ULL<<SEG_D) | (1ULL<<SEG_E) | (1ULL<<SEG_F) | (1ULL<<SEG_G) |
            (1ULL<<DIG_HUNDREDS) | (1ULL<<DIG_TENS) | (1ULL<<DIG_UNITS) |
            (1ULL<<HS_L) | (1ULL<<HS_R),
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&outs);

    gpio_config_t ins = {
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL<<BTN_RIGHT) | (1ULL<<BTN_LEFT),
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&ins);
}

// ==========================
// ADC
// ==========================
static void config_adc(void)
{
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC_CH, ADC_ATTEN_DB_11);
}

// ==========================
// PWM
// ==========================
static void config_pwm(void)
{
    ledc_timer_config_t t = {
        .speed_mode = PWM_SPEED,
        .timer_num = PWM_TIMER_ID,
        .duty_resolution = PWM_RES,
        .freq_hz = PWM_FREQ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&t);

    ledc_channel_config_t ch1 = {
        .gpio_num = LS_L,
        .speed_mode = PWM_SPEED,
        .channel = PWM_CH_L,
        .timer_sel = PWM_TIMER_ID,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&ch1);

    ledc_channel_config_t ch2 = {
        .gpio_num = LS_R,
        .speed_mode = PWM_SPEED,
        .channel = PWM_CH_R,
        .timer_sel = PWM_TIMER_ID,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&ch2);
}

// ==========================
// DISPLAY
// ==========================
static void apagar_digitos(void)
{
    gpio_set_level(DIG_HUNDREDS, 1);
    gpio_set_level(DIG_TENS, 1);
    gpio_set_level(DIG_UNITS, 1);
}

static void activar_digito(int d)
{
    apagar_digitos();
    if (d==0) gpio_set_level(DIG_HUNDREDS,0);
    if (d==1) gpio_set_level(DIG_TENS,0);
    if (d==2) gpio_set_level(DIG_UNITS,0);
}

static void escribir_num(int n)
{
    if(n<0 || n>9) n=0;
    gpio_set_level(SEG_A,map_digits[n][0]);
    gpio_set_level(SEG_B,map_digits[n][1]);
    gpio_set_level(SEG_C,map_digits[n][2]);
    gpio_set_level(SEG_D,map_digits[n][3]);
    gpio_set_level(SEG_E,map_digits[n][4]);
    gpio_set_level(SEG_F,map_digits[n][5]);
    gpio_set_level(SEG_G,map_digits[n][6]);
}

static void tarea_display(void *p)
{
    int pos=0;

    while(1)
    {
        int c = percent/100;
        int d = (percent/10)%10;
        int u = percent%10;

        if(pos==0){
            if(percent>=100){
                escribir_num(c);
                activar_digito(0);
            } else apagar_digitos();
        }
        else if(pos==1){
            if(percent>=10){
                escribir_num(d);
                activar_digito(1);
            } else apagar_digitos();
        }
        else{
            escribir_num(u);
            activar_digito(2);
        }

        pos++;
        if(pos>2) pos=0;

        vTaskDelay(pdMS_TO_TICKS(T_DISP));
    }
}

// ==========================
// MOTOR
// ==========================
static void stop_motor(void)
{
    gpio_set_level(HS_L,0);
    gpio_set_level(HS_R,0);

    ledc_set_duty(PWM_SPEED,PWM_CH_L,0);
    ledc_update_duty(PWM_SPEED,PWM_CH_L);
    ledc_set_duty(PWM_SPEED,PWM_CH_R,0);
    ledc_update_duty(PWM_SPEED,PWM_CH_R);
}

static void leds_dir(dir_t d)
{
    if(d==RIGHT){
        gpio_set_level(LED_GREEN,1);
        gpio_set_level(LED_RED,0);
    } else {
        gpio_set_level(LED_GREEN,0);
        gpio_set_level(LED_RED,1);
    }
}

static void mover_motor(dir_t d, int pwm)
{
    stop_motor();
    if(pwm<=0) return;

    if(d==RIGHT){
        gpio_set_level(HS_L,1);
        gpio_set_level(HS_R,0);

        ledc_set_duty(PWM_SPEED,PWM_CH_L,0);
        ledc_update_duty(PWM_SPEED,PWM_CH_L);
        ledc_set_duty(PWM_SPEED,PWM_CH_R,pwm);
        ledc_update_duty(PWM_SPEED,PWM_CH_R);
    }
    else{
        gpio_set_level(HS_L,0);
        gpio_set_level(HS_R,1);

        ledc_set_duty(PWM_SPEED,PWM_CH_L,pwm);
        ledc_update_duty(PWM_SPEED,PWM_CH_L);
        ledc_set_duty(PWM_SPEED,PWM_CH_R,0);
        ledc_update_duty(PWM_SPEED,PWM_CH_R);
    }
}

static void cambio_seguro(dir_t d)
{
    if(d==dir_current) return;

    stop_motor();
    vTaskDelay(pdMS_TO_TICKS(T_SAFE));

    dir_current=d;
    leds_dir(dir_current);
    mover_motor(dir_current,pwm_val);
}

// ==========================
// ADC TASK
// ==========================
static void tarea_adc(void *p)
{
    while(1)
    {
        int val = adc1_get_raw(ADC_CH);

        if(val<0) val=0;
        if(val>4095) val=4095;

        percent = (val*100)/4095;
        pwm_val = (percent*PWM_MAX)/100;

        mover_motor(dir_current,pwm_val);

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ==========================
// BOTONES
// ==========================
static void tarea_botones(void *p)
{
    bool last_r=1, last_l=1;

    while(1)
    {
        bool r = gpio_get_level(BTN_RIGHT);
        bool l = gpio_get_level(BTN_LEFT);

        if(last_r==1 && r==0) dir_request=RIGHT;
        if(last_l==1 && l==0) dir_request=LEFT;

        if(dir_request!=dir_current){
            cambio_seguro(dir_request);
        }

        last_r=r;
        last_l=l;

        vTaskDelay(pdMS_TO_TICKS(T_BTN));
    }
}

// ==========================
// MAIN
// ==========================
void app_main(void)
{
    config_gpio();
    config_adc();
    config_pwm();

    dir_current=RIGHT;
    dir_request=RIGHT;

    leds_dir(dir_current);
    stop_motor();
    apagar_digitos();

    xTaskCreate(tarea_display,"disp",2048,NULL,1,NULL);
    xTaskCreate(tarea_adc,"adc",2048,NULL,1,NULL);
    xTaskCreate(tarea_botones,"btn",2048,NULL,1,NULL);
}