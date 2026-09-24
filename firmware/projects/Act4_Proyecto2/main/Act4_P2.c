#include <stdio.h>
#include <stdint.h>
#include "stdbool.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Drivers del microcontrolador */
#include "analog_io_mcu.h"
#include "uart_mcu.h"
#include "timer_mcu.h"

/* Definiciones de Constantes */
#define FREC_ADC_US       2000     // 2000 us = 2 ms -> 500 Hz (Muestreo del ADC)
#define FREC_DAC_US       4000     // 4000 us = 4 ms -> 250 Hz (Salida del ECG)
#define UART_BAUD_RATE    115200   // Velocidad de transmisión serie
#define BUFFER_SIZE       231 

/*==================[internal data definition]===============================*/
TaskHandle_t main_task_handle = NULL; // Manejador provisto por la cátedra para la tarea principal
TaskHandle_t dac_task_handle = NULL;

const char ecg[BUFFER_SIZE] = {
    76, 77, 78, 77, 79, 86, 81, 76, 84, 93, 85, 80,
    89, 95, 89, 85, 93, 98, 94, 88, 98, 105, 96, 91,
    99, 105, 101, 96, 102, 106, 101, 96, 100, 107, 101,
    94, 100, 104, 100, 91, 99, 103, 98, 91, 96, 105, 95,
    88, 95, 100, 94, 85, 93, 99, 92, 84, 91, 96, 87, 80,
    83, 92, 86, 78, 84, 89, 79, 73, 81, 83, 78, 70, 80, 82,
    79, 69, 80, 82, 81, 70, 75, 81, 77, 74, 79, 83, 82, 72,
    80, 87, 79, 76, 85, 95, 87, 81, 88, 93, 88, 84, 87, 94,
    86, 82, 85, 94, 85, 82, 85, 95, 86, 83, 92, 99, 91, 88,
    94, 98, 95, 90, 97, 105, 104, 94, 98, 114, 117, 124, 144,
    180, 210, 236, 253, 227, 171, 99, 49, 34, 29, 43, 69, 89,
    89, 90, 98, 107, 104, 98, 104, 110, 102, 98, 103, 111, 101,
    94, 103, 108, 102, 95, 97, 106, 100, 92, 101, 103, 100, 94, 98,
    103, 96, 90, 98, 103, 97, 90, 99, 104, 95, 90, 99, 104, 100, 93,
    100, 106, 101, 93, 101, 105, 103, 96, 105, 112, 105, 99, 103, 108,
    99, 96, 102, 106, 99, 90, 92, 100, 87, 80, 82, 88, 77, 69, 75, 79,
    74, 67, 71, 78, 72, 67, 73, 81, 77, 71, 75, 84, 79, 77, 77, 76, 76,
};

/* --- Callbacks de Interrupción de los Timers --- */
void FuncTimerADC(void *param) {
    // Notifica a la tarea principal (ADCTask)
    vTaskNotifyGiveFromISR(main_task_handle, NULL);
}

void FuncTimerDAC(void *param) {
    // Notifica a la tarea de reproducción del DAC
    vTaskNotifyGiveFromISR(dac_task_handle, NULL);
}

/* --- Tarea: Generación de Señal ECG en el DAC --- */
void DACTask(void *pvParameter) {
    uint8_t i = 0;
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        AnalogOutputWrite((uint8_t)ecg[i]);
        
        i++;
        if (i >= BUFFER_SIZE) {
            i = 0;
        }
    }
}

/* --- Tarea Principal: Lectura de ADC y Transmisión Serie --- */
void ADCTask(void *pvParameter) {
    uint16_t lectura_adc = 0;
    char msg_buffer[32];

    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Digitaliza la entrada analógica en CH1
        AnalogInputReadSingle(CH1, &lectura_adc);

        // Formatea el valor numérico para Serial Oscilloscope
        sprintf(msg_buffer, "%d\r\n", lectura_adc);
        UartSendString(UART_PC, msg_buffer);
    }
}

/* --- Función Principal --- */
void app_main(void) {
    /* 1. Inicialización del puerto serie UART */
    serial_config_t uart_cfg = {
        .port = UART_PC,
        .baud_rate = UART_BAUD_RATE,
        .func_p = NULL,
        .param_p = NULL
    };
    UartInit(&uart_cfg);

    /* 2. Configuración de Entrada Analógica (CH1, lectura puntual) */
    analog_input_config_t adc_cfg = {
        .input = CH1,
        .mode  = ADC_SINGLE
    };
    AnalogInputInit(&adc_cfg);

    /* 3. Inicialización de Salida Analógica (DAC) */
    AnalogOutputInit();

    /* 4. Timer A: Muestreo del ADC a 500 Hz (2000 us) */
    timer_config_t timer_adc = {
        .timer = TIMER_A,
        .period = FREC_ADC_US,
        .func_p = FuncTimerADC,
        .param_p = NULL
    };
    TimerInit(&timer_adc);

    /* 5. Timer B: Generación del DAC a 250 Hz (4000 us) */
    timer_config_t timer_dac = {
        .timer = TIMER_B,
        .period = FREC_DAC_US,
        .func_p = FuncTimerDAC,
        .param_p = NULL
    };
    TimerInit(&timer_dac);

    /* 6. Creación de Tareas FreeRTOS */
    // Asignamos main_task_handle a la tarea principal de lectura/transmisión
    xTaskCreate(ADCTask, "ADCTask", 2048, NULL, 5, &main_task_handle);
    xTaskCreate(DACTask, "DACTask", 2048, NULL, 5, &dac_task_handle);

    /* 7. Inicio de los Timers */
    TimerStart(timer_adc.timer);
    TimerStart(timer_dac.timer);
}