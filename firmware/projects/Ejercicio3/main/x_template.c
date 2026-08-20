/*! @mainpage Template
 *
 * @section genDesc General Description
 *
 * This section describes how the program works.
 *
 * <a href="https://drive.google.com/...">Operation Example</a>
 *
 * @section hardConn Hardware Connection
 *
 * |    Peripheral  |   ESP32       |
 * |:--------------:|:--------------|
 * |    PIN_X       |   GPIO_X      |
 *
 *
 * @section changelog Changelog
 *
 * |    Date     | Description                                    |
 * |:----------:|:-----------------------------------------------|
 * | 12/09/2023 | Document creation                              |
 *
 * @author Albano Peñalva (albano.penalva@uner.edu.ar)
 *
 */

/*==================[inclusions]=============================================*/
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "switch.h" 

/*==================[macros and definitions]=================================*/
#define ON                  1
#define OFF                 2
#define TOGGLE              3

/*==================[typedefs]===============================================*/
// 1. La estructura debe definirse ANTES de usarse en prototipos
typedef struct leds
{
    uint8_t mode;
    uint8_t n_led;
    uint8_t n_ciclos;
    uint16_t n_periodos;
} my_leds;

/*==================[internal functions declaration]=========================*/
void enciende_led(uint8_t n);
void apaga_led(uint8_t n);
void toggle_led(uint8_t n);

void ControlLeds(my_leds *ptr_led);

/*==================[external functions definition]==========================*/
void app_main(void){
    LedsInit();
    SwitchesInit();

    my_leds prueba;
    prueba.mode = TOGGLE;
    prueba.n_led = 2;
    prueba.n_ciclos = 10;
    prueba.n_periodos = 500;

    prueba.n_periodos = 500 / 100;
    ControlLeds(&prueba);
}



/*==================[internal functions definition]==========================*/
void ControlLeds(my_leds *ptr_led) { 
    if (ptr_led == NULL) return;

    switch (ptr_led->mode) {
        case ON:
            if (ptr_led->n_led == 1) enciende_led(1);
            else if (ptr_led->n_led == 2) enciende_led(2);
            else if (ptr_led->n_led == 3) enciende_led(3);
            break;

        case OFF:
            if (ptr_led->n_led == 1) apaga_led(1);
            else if (ptr_led->n_led == 2) apaga_led(2);
            else if (ptr_led->n_led == 3) apaga_led(3);
            break;

        case TOGGLE: {
            for (uint8_t i = 0; i < ptr_led->n_ciclos; i++) {
                if (ptr_led->n_led == 1) {
                    toggle_led(1); 
                } else if (ptr_led->n_led == 2) {
                    toggle_led(2); 
                } else if (ptr_led->n_led == 3) {
                    toggle_led(3); 
                }
                uint16_t j = 0;
                while (j < ptr_led->n_periodos) {
                    j++;
                    vTaskDelay(100 / portTICK_PERIOD_MS); 
                }
            }
            break;
        }
        
        default:
            break;
    }
}


void enciende_led(uint8_t n) {
    if (n == 1) LedOn(LED_1);
    else if (n == 2) LedOn(LED_2);
    else if (n == 3) LedOn(LED_3);
}

void apaga_led(uint8_t n) {
    if (n == 1) LedOff(LED_1);
    else if (n == 2) LedOff(LED_2);
    else if (n == 3) LedOff(LED_3);
}

void toggle_led(uint8_t n) {
    if (n == 1) LedToggle(LED_1);
    else if (n == 2) LedToggle(LED_2);
    else if (n == 3) LedToggle(LED_3);
}


/*==================[end of file]============================================*/