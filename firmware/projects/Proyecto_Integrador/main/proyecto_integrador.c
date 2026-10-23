#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "analog_io_mcu.h"
#include "switch.h"
#include "iir_filter.h"
#include "ble_hid_mcu.h"

/*==================[macros and definitions]=================================*/
#define SAMPLE_FREQ         1000  // 1000 Hz de sobremuestreo
#define DELAY_MEASURE       1     // 1 ms = 1000 Hz
#define DECIMATION_FACTOR   10    // 1000 Hz / 10 = 100 Hz efectivos para el mouse
#define SAMPLES_CALIBRATE   100   // 100 muestras de reposo
#define TOTAL_REPETICIONES  5     // 5 repeticiones por evento

/*==================[typedefs and structures]================================*/
typedef struct {
    uint16_t eog_raw;
    int16_t eog_centrado;
} eog_packet_t;

/*==================[internal data definition]===============================*/
TaskHandle_t adquisicion_task_handle = NULL;
TaskHandle_t procesamiento_task_handle = NULL;

QueueHandle_t eog_queue = NULL;

analog_input_config_t adc_eog_v; // EOG Vertical (CH3)

// Offsets y umbrales de referencia inicializados en 0
int16_t offset_v = 0;

int16_t umbral_arriba_ref = 0;
int16_t umbral_abajo_ref = 0;
int16_t umbral_pestaneo_ref = 0;

bool calibracion_completada = false;

/*==================[internal functions declaration]=========================*/

// Captura 5 repeticiones guiadas por movimiento real sin tiempo fijo
int16_t CalibrarEventoRepeticiones(bool es_positivo) {
    int16_t picos[TOTAL_REPETICIONES] = {0};
    uint8_t rep = 0;

    bool en_movimiento = false;
    int16_t pico_actual = 0;

    uint16_t raw_data;
    float sample_in;
    float filtered_data;
    int16_t centrado;

    // Umbrales de detección de disparo (Trigger)
    int16_t umbral_inicio = es_positivo ? 120 : -120;
    int16_t umbral_retorno = es_positivo ? 80 : -80;

    while (rep < TOTAL_REPETICIONES) {
        AnalogInputReadSingle(adc_eog_v.input, &raw_data);
        
        sample_in = (float)raw_data;
        LowPassFilter(&sample_in, &filtered_data, 1);
        centrado = (int16_t)filtered_data - offset_v;

        // 1. Detectar inicio de una repetición (sale del centro)
        if (!en_movimiento) {
            if ((es_positivo && centrado > umbral_inicio) || (!es_positivo && centrado < umbral_inicio)) {
                en_movimiento = true;
                pico_actual = centrado;
            }
        } 
        // 2. Durante el movimiento: actualizar el valor pico máximo alcanzado
        else {
            if (es_positivo) {
                if (centrado > pico_actual) pico_actual = centrado;
            } else {
                if (centrado < pico_actual) pico_actual = centrado;
            }

            // 3. Detectar retorno al centro (finaliza la repetición)
            if ((es_positivo && centrado < umbral_retorno) || (!es_positivo && centrado > umbral_retorno)) {
                en_movimiento = false;
                picos[rep] = pico_actual;
                rep++; // Avanza a la siguiente repetición
                printf("  -> Repetición %d/5 capturada (Pico: %d cuentas)\n", rep, pico_actual);
            }
        }

        vTaskDelay(DELAY_MEASURE / portTICK_PERIOD_MS);
    }

    // Calcular el promedio exacto de los 5 picos guardados
    int32_t suma_picos = 0;
    for (uint8_t i = 0; i < TOTAL_REPETICIONES; i++) {
        suma_picos += picos[i];
    }

    return (int16_t)(suma_picos / TOTAL_REPETICIONES);
}

void CalibrarEOG(void){
    uint32_t suma_v = 0;
    uint16_t aux_data_v;
    float sample_in;
    float filtered_data;

    calibracion_completada = false;

    // --- CALIBRACIÓN 1: REPOSO (MIRADA AL FRENTE) ---
    printf("\n=== Comience Calibración 1: Reposo ===\n");
    printf("Mire hacia el frente y presione Switch 1...\n");
    while(SwitchesRead() != SWITCH_1){
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    for(uint8_t i = 0; i < SAMPLES_CALIBRATE; i++){
        AnalogInputReadSingle(adc_eog_v.input, &aux_data_v);
        sample_in = (float)aux_data_v;
        LowPassFilter(&sample_in, &filtered_data, 1);
        suma_v += (uint16_t)filtered_data;
        vTaskDelay(DELAY_MEASURE / portTICK_PERIOD_MS);
    }
    offset_v = (int16_t)(suma_v / SAMPLES_CALIBRATE);
    printf("Finalizó la Calibración 1. Offset de reposo: %d cuentas\n", offset_v);

    // --- CALIBRACIÓN 2: MIRADA ARRIBA ---
    printf("\n=== Comience Calibración 2: Mirada Arriba ===\n");
    printf("Presione Switch 1 y realice 5 miradas hacia arriba volviendo al centro...\n");
    while(SwitchesRead() != SWITCH_1){
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    umbral_arriba_ref = CalibrarEventoRepeticiones(true);
    printf("Finalizó la Calibración 2. Referencia Arriba: %d cuentas\n", umbral_arriba_ref);

    // --- CALIBRACIÓN 3: MIRADA ABAJO ---
    printf("\n=== Comience Calibración 3: Mirada Abajo ===\n");
    printf("Presione Switch 1 y realice 5 miradas hacia abajo volviendo al centro...\n");
    while(SwitchesRead() != SWITCH_1){
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    umbral_abajo_ref = CalibrarEventoRepeticiones(false);
    printf("Finalizó la Calibración 3. Referencia Abajo: %d cuentas\n", umbral_abajo_ref);

    // --- CALIBRACIÓN 4: PESTAÑEO ---
    printf("\n=== Comience Calibración 4: Pestañeo ===\n");
    printf("Presione Switch 1 y realice 5 pestañeos firmes...\n");
    while(SwitchesRead() != SWITCH_1){
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    umbral_pestaneo_ref = CalibrarEventoRepeticiones(true);
    printf("Finalizó la Calibración 4. Referencia Pestañeo: %d cuentas\n", umbral_pestaneo_ref);

    printf("\n==========================================");
    printf("\n¡Todas las calibraciones finalizadas!");
    printf("\nIniciando adquisición en tiempo real...\n");
    printf("==========================================\n\n");

    calibracion_completada = true;
}

void UpdateMouse(int8_t * pos, uint8_t * click_flag, int16_t eog_centrado){
    *click_flag = 0;

    // Si la calibración no ha finalizado, no mover el mouse
    if (!calibracion_completada) {
        *pos = 0;
        return;
    }

    // Clasificación de eventos usando las referencias calculadas
    if(umbral_pestaneo_ref > 0 && eog_centrado > umbral_pestaneo_ref){
        *click_flag = 1;
        *pos = 0; // Al pestañear solo hacemos click sin mover el eje Y
    }
    else if(umbral_arriba_ref > 0 && eog_centrado >= (umbral_arriba_ref * 0.5)){
        *pos = (eog_centrado >= umbral_arriba_ref) ? -30 : -10;
    }
    else if(umbral_abajo_ref < 0 && eog_centrado <= (umbral_abajo_ref * 0.5)){
        *pos = (eog_centrado <= umbral_abajo_ref) ? 30 : 10;
    }
    else{
        *pos = 0;
    }
}

/*==================[Tasks]==================================================*/

// Tarea 1: Adquisición a 1000 Hz, Filtrado IIR y Decimación
void AdquisicionTask(void *pvParameter){
    static uint8_t decimation_counter = 0;

    uint16_t eog_v_data;
    float sample_in;
    float eog_v_filtered;
    eog_packet_t packet;

    CalibrarEOG();

    while(true){
        // 1. Muestreo a 1000 Hz
        AnalogInputReadSingle(adc_eog_v.input, &eog_v_data);

        // 2. Conversión a float e ingreso al filtro (Chunk size = 1)
        sample_in = (float)eog_v_data;
        LowPassFilter(&sample_in, &eog_v_filtered, 1);

        // 3. Decimación (Mandar dato a la cola a 100 Hz efectivos)
        decimation_counter++;
        if (decimation_counter >= DECIMATION_FACTOR) {
            decimation_counter = 0;

            packet.eog_raw = eog_v_data;
            packet.eog_centrado = (int16_t)eog_v_filtered - offset_v;

            // Enviar paquete a la cola de procesamiento
            xQueueSend(eog_queue, &packet, 0);
        }

        vTaskDelay(DELAY_MEASURE / portTICK_PERIOD_MS);
    }
}

// Tarea 2: Procesamiento de datos recibidos por la cola, envío BLE y consola
void ProcesamientoOutputTask(void *pvParameter){
    eog_packet_t packet_recibido;
    int8_t mouse_y = 0;
    uint8_t mouse_click = 0;

    while(true){
        // Bloquea hasta que la Tarea de Adquisición envíe un nuevo paquete
        if (xQueueReceive(eog_queue, &packet_recibido, portMAX_DELAY) == pdTRUE) {
            UpdateMouse(&mouse_y, &mouse_click, packet_recibido.eog_centrado);

            // Transmisión por Bluetooth HID solo si finalizó la calibración
            if (calibracion_completada) {
                if (mouse_click) {
                    BleHidSendMouse(HID_MOUSE_LEFT, 0, 0);   // Evento Clic Izquierdo
                } else if (mouse_y != 0) {
                    BleHidSendMouse(0, 0, mouse_y); // Movimiento en Y (Dx=0, Dy=mouse_y)
                }
            }

            printf("%d,%d,%d\n", packet_recibido.eog_raw, packet_recibido.eog_centrado, mouse_y);
        }
    }
}

/*==================[external functions definition]==========================*/
void app_main(void){
    SwitchesInit();
    BleHidInit("EOG_Mouse"); // Inicialización de Bluetooth HID Mouse con nombre visible

    adc_eog_v.input = CH3;
    adc_eog_v.mode = ADC_SINGLE;
    AnalogInputInit(&adc_eog_v); 

    // Inicialización del filtro IIR Pasabajos: Fs = 1000 Hz, Fc = 28 Hz, Orden 2
    LowPassInit(SAMPLE_FREQ, 28, ORDER_2);

    // Crear la Queue para 10 elementos de tipo eog_packet_t
    eog_queue = xQueueCreate(10, sizeof(eog_packet_t));

    if (eog_queue != NULL) {
        // Tarea de Adquisición: Prioridad 5 (Alta)
        xTaskCreate(&AdquisicionTask, "ADQUISICION_EOG", 4096, NULL, 5, &adquisicion_task_handle);

        // Tarea de Procesamiento / Salida: Prioridad 4 (Media)
        xTaskCreate(&ProcesamientoOutputTask, "PROCESAMIENTO_MOUSE", 4096, NULL, 4, &procesamiento_task_handle);
    }
}