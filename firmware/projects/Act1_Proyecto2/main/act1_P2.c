/** @file act1_P2.c
 *  @brief Proyecto Medidor de distancia por ultrasonido con HC-SR04, LEDs y LCD.
 *  Aplicación basada en FreeRTOS compuesta por tres tareas independientes:
 *  - TECs: Lee los pulsadores TEC1 (medición) y TEC2 (hold).
 *  - ULTRASONIDO: Realiza mediciones de distancia de forma continua.
 *  - LedsyLcd: Maneja el encendido de LEDs según rango y la pantalla LCD.
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

/*==================[macros and definitions]=================================*/
#define CONFIG_BLINK_PERIOD_REFRESH_MS 1000  /* Refresco de medición y pantalla: 1s (Consigna) */
#define CONFIG_BLINK_PERIOD_TECS_MS 50       /* Tasa de refresco de lectura de teclas */
#define CONFIG_BLINK_PERIOD_US_MS 200        /* Tasa de refresco de lectura del sensor ultrasónico */
/*==================[internal data definition]===============================*/
/** @brief Variable global que indica si la medición está activa. */
bool MEDICION = true;

/** @brief Variable global que indica si la pantalla LCD está congelada (HOLD). */
bool HOLD = false;

/** @brief Variable global que almacena el valor actual de la distancia en cm. */
uint16_t DISTANCIA = 0;

/*==================[internal functions declaration]=========================*/
/**
 * @brief Tarea encargada de leer el estado de las teclas (TEC1 y TEC2).
 * @param pvParameter Puntero a parámetros de la tarea (no utilizado).
 */
static void TECsTask(void *pvParameter);

/**
 * @brief Tarea encargada de realizar la lectura del sensor ultrasónico HC-SR04.
 * @param pvParameter Puntero a parámetros de la tarea (no utilizado).
 */
static void UltrasonidoTask(void *pvParameter);

/**
 * @brief Tarea encargada del control de LEDs y la actualización de la pantalla LCD.
 * @param pvParameter Puntero a parámetros de la tarea (no utilizado).
 */
static void LedsyLcdTask(void *pvParameter);

/*==================[internal functions definition]==========================*/

static void TECsTask(void *pvParameter)
{
    while(true)
    {
        uint8_t teclas = SwitchesRead();

        // Control de TEC1 (Cambiar Estado MEDICION)
        if(teclas & SWITCH_1)
        {
            MEDICION = !MEDICION;
        }
        // Control de TEC2 (Cambiar Estado HOLD)
        if(teclas & SWITCH_2)
        {
            HOLD = !HOLD;
        }
        vTaskDelay(CONFIG_BLINK_PERIOD_TECS_MS / portTICK_PERIOD_MS);
    }
}

static void UltrasonidoTask(void *pvParameter)
{
    while(true)
    {
        if(MEDICION)
        {
            // Medir distancia en centímetros con el driver HC-SR04
            DISTANCIA = HcSr04ReadDistanceInCentimeters();
        }

        vTaskDelay(CONFIG_BLINK_PERIOD_US_MS / portTICK_PERIOD_MS);
    }
}

static void LedsyLcdTask(void *pvParameter)
{
    while(true)
    {
        if(MEDICION)
        {
            // Control de LEDs por rangos de distancia
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

            // Control del Display LCD
            if(!HOLD)
            {
                LcdItsE0803Write(DISTANCIA);
            }
        }
        else
        {
            // Si MEDICION == No, apagar LEDs y limpiar LCD
            LedsOffAll();
            LcdItsE0803Off();
        }

        // Refresco de medición: 1 segundo
        vTaskDelay(CONFIG_BLINK_PERIOD_REFRESH_MS / portTICK_PERIOD_MS);
    }
}

/*==================[external functions definition]==========================*/

void app_main(void)
{
    // Inicialización de Periféricos
    LedsInit();
    SwitchesInit();
    HcSr04Init(GPIO_3, GPIO_2); // Pines Trigger y Echo según correspondan en el board
    LcdItsE0803Init();

    // Creación de Tareas FreeRTOS
    xTaskCreate(&TECsTask, "TECs", 2048, NULL, 5, NULL);
    xTaskCreate(&UltrasonidoTask, "ULTRASONIDO", 2048, NULL, 5, NULL);
    xTaskCreate(&LedsyLcdTask, "LedsyLcd", 2048, NULL, 5, NULL);
}