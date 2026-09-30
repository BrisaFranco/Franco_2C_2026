/** @file medidor_distancia_int.c
 *  @brief Proyecto Medidor de distancia por ultrasonido controlado por temporizador e interrupciones.
 *  
 *  Aplicación basada en FreeRTOS que realiza mediciones con un sensor HC-SR04 y permite la
 *  interacción mediante los pulsadores (TEC1 y TEC2) o comandos enviados por puerto serie UART.
 *  Soporta funcionalidades avanzadas como cambio de unidades (cm/pulgadas), modo de valor máximo,
 *  congelado de pantalla (HOLD) y ajuste dinámico del período de refresco.
 * 
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
#define CONFIG_PERIOD_REFRESH_US 1000000 /* Periodo inicial de medición: 1 segundo */
#define PERIOD_STEP_US           100000  /* Paso de cambio de periodo: 100 ms */
#define MIN_PERIOD_US            250000  /* Límite inferior de periodo: 250 ms */
#define MAX_PERIOD_US            3000000 /* Límite superior de periodo: 3 s */
#define UART_BAUD_RATE           115200  /* Velocidad de comunicación UART */

/*==================[internal data definition]===============================*/
/** @brief Variable global que indica si la medición está activa. */
volatile bool medicion = true;

/** @brief Variable global que indica si la pantalla y los envíos están congelados (HOLD). */
volatile bool hold = false;

/** @brief Variable global que almacena el valor actual de la distancia en cm. */
volatile uint16_t distancia = 0;

/** @brief Variable global que indica la unidad de medida utilizada (false: cm, true: pulgadas). */
volatile bool es_pulgadas = false;

/** @brief Variable global que habilita o deshabilita el modo de visualización de valor máximo. */
volatile bool modo_maximo = false;

/** @brief Variable global que almacena el valor máximo de distancia registrado en cm. */
volatile uint16_t distancia_maxima = 0;

/** @brief Variable global que guarda el período actual del temporizador en microsegundos. */
volatile uint32_t periodo_us = CONFIG_PERIOD_REFRESH_US;

/** @brief Handle de la tarea FreeRTOS encargada de la medición y procesamiento. */
TaskHandle_t medidor_task_handle = NULL;

/*==================[internal functions declaration]=========================*/
/**
 * @brief Tarea principal que procesa los comandos UART, realiza las mediciones,
 *        actualiza los LEDs, la pantalla LCD y envía la información por puerto serie.
 * @param pvParameter Puntero a parámetros de la tarea (no utilizado).
 */
static void MedidorTask(void *pvParameter);

/**
 * @brief Función de callback invocada periódicamente por la interrupción del Timer A.
 *        Notifica a la tarea principal para iniciar un nuevo ciclo de trabajo.
 * @param param Puntero a parámetros del timer (no utilizado).
 */
void FuncTimerMedidor(void *param);

/**
 * @brief Función de callback invocada por la interrupción del pulsador TEC1 (Switch 1).
 *        Alterna el estado de activación de la medición.
 * @param param Puntero a parámetros de la interrupción (no utilizado).
 */
void FuncTec1(void *param);

/**
 * @brief Función de callback invocada por la interrupción del pulsador TEC2 (Switch 2).
 *        Alterna el estado de congelado de pantalla (HOLD).
 * @param param Puntero a parámetros de la interrupción (no utilizado).
 */
void FuncTec2(void *param);

/*==================[ISRs / Callbacks]=======================================*/

void FuncTimerMedidor(void *param)
{
    vTaskNotifyGiveFromISR(medidor_task_handle, NULL);
}

void FuncTec1(void *param)
{
    medicion = !medicion;
}

void FuncTec2(void *param)
{
    hold = !hold;
}

/*==================[tasks definition]======================================*/

static void MedidorTask(void *pvParameter)
{
    uint8_t comando;
    uint16_t valor_mostrar = 0;

    while (true)
    {
        /* Espera la notificación enviada por la ISR del Timer */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

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
                    break;
            }
        }

        if (medicion)
        {
            if (!hold)
            {
                distancia = HcSr04ReadDistanceInCentimeters();
                if (distancia > distancia_maxima)
                {
                    distancia_maxima = distancia;
                }
            }

            // 1. Control de LEDs según el valor crudo en centímetros
            if (distancia < 10)
            {
                LedsOffAll();
            }
            else if (distancia >= 10 && distancia < 20)
            {
                LedOn(LED_1);
                LedOff(LED_2);
                LedOff(LED_3);
            }
            else if (distancia >= 20 && distancia < 30)
            {
                LedOn(LED_1);
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
            {
                valor_mostrar = (uint16_t)(valor_mostrar / 2.54f);
            }

            // 4. Actualización del Display LCD
            if (!hold)
            {
                LcdItsE0803Write(valor_mostrar);
            }

            // 5. Envío por UART con formato de 3 dígitos ASCII
            if (valor_mostrar < 10)
            {
                UartSendString(UART_PC, "00");
            }
            else if (valor_mostrar < 100)
            {
                UartSendString(UART_PC, "0");
            }

            UartSendString(UART_PC, (char *)UartItoa(valor_mostrar, 10));

            if (es_pulgadas)
            {
                UartSendString(UART_PC, " inch\r\n");
            }
            else
            {
                UartSendString(UART_PC, " cm\r\n");
            }
        }
        else
        {
            LedsOffAll();
            LcdItsE0803Off();
        }
    }
}

/*==================[external functions definition]==========================*/

/**
 * @brief Función principal de la aplicación.
 *        Configura e inicializa periféricos (LEDs, Switches, HC-SR04, LCD, UART, Timer),
 *        asocia las interrupciones y crea la tarea de medición en FreeRTOS.
 */
void app_main(void)
{
    // Configuración e inicialización de puerto serie UART
    serial_config_t uart_config = {
        .port = UART_PC,
        .baud_rate = UART_BAUD_RATE,
        .func_p = UART_NO_INT,
        .param_p = NULL
    };

    // Inicialización de Periféricos
    LedsInit();
    SwitchesInit();
    HcSr04Init(GPIO_3, GPIO_2);
    LcdItsE0803Init();
    UartInit(&uart_config);

    // Configuración de interrupciones para pulsadores
    SwitchActivInt(SWITCH_1, &FuncTec1, NULL);
    SwitchActivInt(SWITCH_2, &FuncTec2, NULL);

    // Creación de Tarea FreeRTOS
    xTaskCreate(&MedidorTask, "MedidorTask", 2048, NULL, 5, &medidor_task_handle);

    // Configuración e inicio del Timer para control periódico de mediciones
    timer_config_t timer_medidor = {
        .timer = TIMER_A,
        .period = CONFIG_PERIOD_REFRESH_US,
        .func_p = FuncTimerMedidor,
        .param_p = NULL
    };
    TimerInit(&timer_medidor);
    TimerStart(timer_medidor.timer);
}