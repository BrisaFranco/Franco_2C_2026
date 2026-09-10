/** @file medidor_distancia_int.c
 *  @brief Proyecto Medidor de distancia por ultrasonido con HC-SR04, LEDs y LCD usando Interrupciones y Timers.
 *  Aplicación basada en FreeRTOS compuesta por:
 *  - Interrupciones de Teclas (GPIO): Cambio de estado de MEDICION (TEC1) y HOLD (TEC2).
 *  - Timer A: Dispara periódicamente la tarea de medición y actualización (cada 1 s). 
 *  @author Brisa Franco
 *  @date 2026
 */
/*==================[inclusions]=============================================*/
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "switch.h"
#include "hc_sr04.h"
#include "lcditse0803.h"
#include "timer_mcu.h"

/*==================[macros and definitions]=================================*/
#define CONFIG_PERIOD_REFRESH_US 1000000 /* Periodo de refresco: 1 segundo (en microsegundos) */

/*==================[internal data definition]===============================*/
bool MEDICION = true;
bool HOLD = false;
uint16_t DISTANCIA = 0;

/* Handle para notificar a la tarea desde la ISR del Timer */
TaskHandle_t medidor_task_handle = NULL;

/*==================[internal functions declaration]=========================*/
static void MedidorTask(void *pvParameter);

void FuncTimerMedidor(void *param);
void FuncTec1(void *param);
void FuncTec2(void *param);

/*==================[ISRs / Callbacks]=======================================*/

/** @brief ISR invocada periódicamente por el Timer A (cada 1 segundo). */
void FuncTimerMedidor(void *param)
{
    vTaskNotifyGiveFromISR(medidor_task_handle, NULL);
}
/** @brief ISR invocada al presionar TEC1. */
void FuncTec1(void *param)
{
    MEDICION = !MEDICION;
}
/** @brief ISR invocada al presionar TEC2. */
void FuncTec2(void *param)
{
    HOLD = !HOLD;
}
/*==================[tasks definition]======================================*/
static void MedidorTask(void *pvParameter)
{
    while(true)
    {
        /* Espera la notificación de la ISR del Timer (se despierta cada 1s) */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if(MEDICION)
        {
            // 1. Lectura del sensor
            DISTANCIA = HcSr04ReadDistanceInCentimeters();
            // 2. Control de LEDs según rango de distancia
            if(DISTANCIA < 10)
            {
                LedsOffAll();
            }
            else if(DISTANCIA >= 10 && DISTANCIA < 20)
            {
                LedOn(LED_1);
                LedOff(LED_2);
                LedOff(LED_3);
            }
            else if(DISTANCIA >= 20 && DISTANCIA < 30)
            {
                LedOn(LED_1);
                LedOn(LED_2);
                LedOff(LED_3);
            }
            else if(DISTANCIA >= 30)
            {
                LedOn(LED_1);
                LedOn(LED_2);
                LedOn(LED_3);
            }
            // 3. Control de la pantalla LCD (solo si HOLD no está activo)
            if(!HOLD)
            {
                LcdItsE0803Write(DISTANCIA);
            }
        }
        else
        {
            // Si MEDICION == false, apaga LEDs y LCD
            LedsOffAll();
            LcdItsE0803Off();
        }
    }
}
/*==================[external functions definition]==========================*/
void app_main(void)
{
    // Inicialización de Periféricos
    LedsInit();
    SwitchesInit();
    HcSr04Init(GPIO_3, GPIO_2);
    LcdItsE0803Init();

    // Configuración de Interrupciones en Teclas (TEC1 y TEC2)
    SwitchActivInt(SWITCH_1, &FuncTec1, NULL);
    SwitchActivInt(SWITCH_2, &FuncTec2, NULL);

    // Creación de la Tarea principal guardando su Handle
    xTaskCreate(&MedidorTask, "MedidorTask", 2048, NULL, 5, &medidor_task_handle);

    // Configuración e inicio del Timer de Hardware (1 segundo)
    timer_config_t timer_medidor = {
        .timer = TIMER_A,
        .period = CONFIG_PERIOD_REFRESH_US,
        .func_p = FuncTimerMedidor,
        .param_p = NULL
    };
    TimerInit(&timer_medidor);
    TimerStart(timer_medidor.timer);
}