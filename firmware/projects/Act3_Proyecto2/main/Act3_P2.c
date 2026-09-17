/** @file medidor_distancia_int.c
 *  @brief Proyecto Medidor de distancia por ultrasonido con comandos UART extendidos ('O','H','I', 'M', 'F', 'S').
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
#include "uart_mcu.h"

/*==================[macros and definitions]=================================*/
#define CONFIG_PERIOD_REFRESH_US 1000000 /* Periodo inicial: 1 segundo */
#define PERIOD_STEP_US           100000  /* Paso de cambio: 100 ms */
#define MIN_PERIOD_US            250000  /* Límite inferior: 250 ms */
#define MAX_PERIOD_US            3000000 /* Límite superior: 3 s */
#define UART_BAUD_RATE           115200

/*==================[internal data definition]===============================*/
volatile bool medicion = true;
volatile bool hold = false;
volatile uint16_t distancia = 0;

/* Variables para los nuevos comandos */
volatile bool es_pulgadas = false;
volatile bool modo_maximo = false;
volatile uint16_t distancia_maxima = 0;
volatile uint32_t periodo_us = CONFIG_PERIOD_REFRESH_US;

TaskHandle_t medidor_task_handle = NULL;

/*==================[internal functions declaration]=========================*/
static void MedidorTask(void *pvParameter);

void FuncTimerMedidor(void *param);
void FuncTec1(void *param);
void FuncTec2(void *param);

/*==================[ISRs / Callbacks]=======================================*/
/** @brief ISR invocada periódicamente por el Timer A. */
void FuncTimerMedidor(void *param)
{vTaskNotifyGiveFromISR(medidor_task_handle, NULL);
}

/** @brief ISR invocada al presionar TEC1. */
void FuncTec1(void *param)
{medicion = !medicion;
}

/** @brief ISR invocada al presionar TEC2. */
void FuncTec2(void *param)
{hold = !hold;
}

/*==================[tasks definition]======================================*/
static void MedidorTask(void *pvParameter)
{
    uint8_t comando;
    uint16_t valor_mostrar = 0;

    while (true)
    {ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* --- Recepción de comandos por puerto serie --- */
        if (UartReadByte(UART_PC, &comando))
        {
            switch (comando)
            {
                case 'O':
                case 'o':
                    medicion = !medicion;
                    break;

                case 'H':
                case 'h':
                    hold = !hold;
                    break;

                case 'I':
                case 'i':
                    es_pulgadas = !es_pulgadas;
                    break;

                case 'M':
                case 'm':
                    modo_maximo = !modo_maximo;
                    if (!modo_maximo)
                    {
                        distancia_maxima = 0; /* Reinicia el máximo al desactivar el modo */
                    }
                    break;

                case 'F':
                case 'f':
                    if (periodo_us > MIN_PERIOD_US)
                    {
                        periodo_us -= PERIOD_STEP_US;
                        TimerUpdatePeriod(TIMER_A, periodo_us);
                    }
                    break;

                case 'S':
                case 's':
                    if (periodo_us < MAX_PERIOD_US)
                    {
                        periodo_us += PERIOD_STEP_US;
                        TimerUpdatePeriod(TIMER_A, periodo_us);
                    }
                    break;

                default:
                    break;}
        }

        if (medicion)
        {if (!hold)
            {
                distancia = HcSr04ReadDistanceInCentimeters();
                if (distancia > distancia_maxima)
                {
                    distancia_maxima = distancia;
                }
            }

            // 1. Control de LEDs según el valor crudo en centímetros
            if (distancia < 10)
            {LedsOffAll();
            }
            else if (distancia >= 10 && distancia < 20)
            {LedOn(LED_1);
        	LedOff(LED_2);
            LedOff(LED_3);
            }
            else if (distancia >= 20 && distancia < 30)
            {LedOn(LED_1);
        	LedOn(LED_2);
            LedOff(LED_3);
            }
            else if (distancia >= 30)
            {
            LedOn(LED_1);
            LedOn(LED_2);
            LedOn(LED_3);
            }

            // 2. Selección del valor a procesar (Medición actual vs. Máximo)
            valor_mostrar = modo_maximo ? distancia_maxima : distancia;

            // 3. Conversión de unidad si aplica (1 pulgada = 2.54 cm)
            if (es_pulgadas)
            {valor_mostrar = (uint16_t)(valor_mostrar / 2.54f);
            }
            // 4. Actualización del Display LCD
            if (!hold)
            {LcdItsE0803Write(valor_mostrar);
            }
            // 5. Envío por UART con formato de 3 dígitos ASCII
            if (valor_mostrar < 10)
            { UartSendString(UART_PC, "00");
            }
            else if (valor_mostrar < 100)
            {UartSendString(UART_PC, "0");
            }
            UartSendString(UART_PC, (char *)UartItoa(valor_mostrar, 10));
            if (es_pulgadas)
            {UartSendString(UART_PC, " inch\r\n");
            }
            else
            {UartSendString(UART_PC, " cm\r\n");
            }
        }
        else
        {LedsOffAll();
		LcdItsE0803Off();
        }
    }
}

/*==================[external functions definition]==========================*/
void app_main(void)
{
    serial_config_t uart_config = {
        .port = UART_PC,
        .baud_rate = UART_BAUD_RATE,
        .func_p = UART_NO_INT,
        .param_p = NULL
    };

    LedsInit();
    SwitchesInit();
    HcSr04Init(GPIO_3, GPIO_2);
    LcdItsE0803Init();
    UartInit(&uart_config);

    SwitchActivInt(SWITCH_1, &FuncTec1, NULL);
    SwitchActivInt(SWITCH_2, &FuncTec2, NULL);

    xTaskCreate(&MedidorTask, "MedidorTask", 2048, NULL, 5, &medidor_task_handle);

    timer_config_t timer_medidor = {
        .timer = TIMER_A,
        .period = CONFIG_PERIOD_REFRESH_US,
        .func_p = FuncTimerMedidor,
        .param_p = NULL
    };
    TimerInit(&timer_medidor);
    TimerStart(timer_medidor.timer);
}