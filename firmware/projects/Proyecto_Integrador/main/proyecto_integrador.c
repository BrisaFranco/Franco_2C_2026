/**
 * @file ej_bluetooth_mouse_kb.c
 * @brief Firmware para el control de un Mouse Bluetooth HID guiado por EOG en ESP32-C6.
 *        Calibración guiada de 40 s reales (4 etapas de 10 s), muestreando a 1 kHz con un
 *        timer de hardware (timer_mcu).
 *
 * Funcionamiento:
 *  - Calibración: reposo (offset + ruido), mirada arriba, mirada abajo y pestañeo.
 *    En arriba/abajo/pestañeo se detecta cada pico individual y la referencia es el
 *    PROMEDIO de los picos (no el máximo absoluto).
 *  - Operación: "mirar arriba y volver al centro" = 1 movimiento (se emite al volver).
 *    Tras cada movimiento o click hay un bloqueo hasta que la señal se estabiliza en el
 *    centro, para que el rebote/cola del movimiento anterior no cuente como otro.
 *
 * @author Brisa Franco
 * @date Año 2026
 */

/*==================[inclusions]=============================================*/
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_attr.h"

#include "timer_mcu.h"
#include "analog_io_mcu.h"
#include "switch.h"
#include "iir_filter.h"
#include "ble_hid_mcu.h"

/*==================[macros and definitions]=================================*/
#define SAMPLE_FREQ             1000    // Frecuencia de muestreo [Hz]
#define SAMPLE_PERIOD_US        (1000000 / SAMPLE_FREQ)
#define DECIMATION_FACTOR       10      // 1000 Hz / 10 = 100 Hz de salida

#define CALIB_SEC               10      // Duración de cada etapa de calibración [s]
#define MUESTRAS_CALIB          (SAMPLE_FREQ * CALIB_SEC)
#define WARMUP_MUESTRAS         200     // Muestras descartadas al iniciar cada etapa

/* --- Detección de picos durante la calibración --- */
#define K_RUIDO_DETECCION       5.0f    // Un pico empieza cuando la señal supera K * sigma(reposo)
#define UMBRAL_DET_MIN_CUENTAS  30.0f   // Piso del umbral de detección [cuentas]
#define PICOS_MIN_VALIDOS       3       // Con menos picos se avisa y se usa el máximo absoluto

/* --- Detección en operación (fracciones del pico promedio, a 100 Hz) --- */
#define FRAC_DETECCION          0.6f    // Se considera "mirada arriba/abajo" al superar esta fracción
#define FRAC_RETORNO            0.25f   // Se considera "volvió al centro" al bajar de esta fracción
#define FRAC_PESTANEO           0.6f    // Umbral de click respecto del pico promedio de pestañeo
#define REARME_MUESTRAS         8       // Muestras seguidas en el centro para rearmar (80 ms)
#define BLOQUEO_MAX_MUESTRAS    200     // Seguro: rearme forzado tras 2 s
#define ESPERA_RETORNO_MAX      500     // Seguro: si la mirada se sostiene más de 5 s, se descarta

#define PASO_MOUSE_PX           20      // Desplazamiento por movimiento completado
#define CLICK_PULSO_MS          20      // Tiempo que se mantiene presionado el botón

#define QUEUE_LEN               32

/*==================[typedefs and structures]================================*/
typedef enum {
    ESTADO_CENTRO,                      // Ojo en el centro, esperando un movimiento
    ESTADO_ESPERANDO_RETORNO_ARRIBA,    // Mirando arriba, esperando que vuelva al centro
    ESTADO_ESPERANDO_RETORNO_ABAJO,     // Mirando abajo, esperando que vuelva al centro
    ESTADO_BLOQUEO                      // Movimiento/click hecho: espera a que la señal se asiente
} estado_movimiento_t;

typedef struct {
    uint16_t raw;
    int16_t  centrado;
} muestra_eog_t;

/*==================[internal data definition]===============================*/
static TaskHandle_t adquisicion_task_handle = NULL;
static SemaphoreHandle_t sem_switch = NULL;
static QueueHandle_t cola_muestras = NULL;
static analog_input_config_t adc_eog_v;
static int16_t offset_v = 0;
static float ruido_sigma = 0.0f;                // Desvío estándar de la señal en reposo [cuentas]
static int16_t umbral_arriba_ref = 0;           // Pico PROMEDIO mirando arriba (positivo)
static int16_t umbral_abajo_ref = 0;            // Pico PROMEDIO mirando abajo (negativo)
static int16_t umbral_pestaneo_ref = 0;         // Pico PROMEDIO de pestañeo (positivo)
static volatile bool calibracion_completada = false;
static estado_movimiento_t estado_actual = ESTADO_CENTRO;

/*==================[internal functions definition]==========================*/
/** * @brief ISR del TIMER_A (se ejecuta cada 1 ms): despierta a la tarea de adquisición.*/

static void IRAM_ATTR TimerMuestreoISR(void *param) {
    if (adquisicion_task_handle != NULL) {
        vTaskNotifyGiveFromISR(adquisicion_task_handle, NULL);
    }
}

/** @brief Descarta ticks acumulados e inicia el muestreo periódico de 1 kHz. */
static void IniciarMuestreo(void) {
    TimerStop(TIMER_A);
    TimerReset(TIMER_A);
    ulTaskNotifyTake(pdTRUE, 0);
    TimerStart(TIMER_A);
}

/** @brief Detiene el muestreo. */
static void DetenerMuestreo(void) {
    TimerStop(TIMER_A);
}

/** @brief Bloquea la tarea hasta el próximo tick de 1 ms. */
static inline void EsperarTick(void) {
    ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
}

/** @brief Bloquea la tarea hasta que se presione SWITCH_1. */
static void EsperarSwitch1(void) {
    xSemaphoreTake(sem_switch, 0);
    xSemaphoreTake(sem_switch, portMAX_DELAY);
}

/** @brief Lee el ADC y aplica el filtro pasa-bajos. Devuelve la muestra filtrada. */
static float LeerMuestraFiltrada(uint16_t *raw_out) {
    uint16_t raw = 0;
    float entrada, salida = 0.0f;

    AnalogInputReadSingle(adc_eog_v.input, &raw);
    entrada = (float)raw;
    LowPassFilter(&entrada, &salida, 1);

    if (raw_out != NULL) {
        *raw_out = raw;
    }
    return salida;
}

/** @brief Inicia el muestreo y descarta muestras iniciales para que el filtro IIR se asiente. */
static void IniciarMedicion(void) {
    IniciarMuestreo();
    for (uint32_t i = 0; i < WARMUP_MUESTRAS; i++) {
        EsperarTick();
        (void)LeerMuestraFiltrada(NULL);
    }
}

/**
 * @brief Etapa de reposo: calcula el offset (promedio) y el ruido (desvío estándar).
 * @return Offset de reposo en cuentas.
 */
static int16_t MedirReposo(void) {
    double suma = 0.0, suma_cuad = 0.0;

    IniciarMedicion();

    for (uint32_t i = 0; i < MUESTRAS_CALIB; i++) {
        EsperarTick();
        uint16_t raw = 0;
        float filtrada = LeerMuestraFiltrada(&raw);

        suma += (double)filtrada;
        suma_cuad += (double)filtrada * (double)filtrada;

        if (((i + 1) % SAMPLE_FREQ) == 0) {
            printf(" Segundos: %lu/%d (ADC crudo: %u | filtrada: %.1f)\r\n",
                   (unsigned long)((i + 1) / SAMPLE_FREQ), CALIB_SEC, (unsigned)raw, filtrada);
        }
    }

    DetenerMuestreo();

    double media = suma / (double)MUESTRAS_CALIB;
    double varianza = suma_cuad / (double)MUESTRAS_CALIB - media * media;
    if (varianza < 0.0) varianza = 0.0;
    ruido_sigma = (float)sqrt(varianza);

    printf("--> Ruido en reposo (sigma): %.1f cuentas\r\n", ruido_sigma);
    return (int16_t)media;
}

/**
 * @brief Mide durante CALIB_SEC segundos y devuelve el PROMEDIO de los picos individuales.
 * @param signo     +1 para picos positivos (arriba, pestañeo), -1 para negativos (abajo).
 * @param etiqueta  Texto para el progreso por terminal.
 * @return          Pico promedio en cuentas, con su signo original.
 */
static int16_t MedirPicos(int signo, const char *etiqueta) {
    float umbral_alto = K_RUIDO_DETECCION * ruido_sigma;
    if (umbral_alto < UMBRAL_DET_MIN_CUENTAS) umbral_alto = UMBRAL_DET_MIN_CUENTAS;
    const float umbral_bajo = umbral_alto * 0.5f;

    bool en_pico = false;
    float pico_actual = 0.0f;
    float extremo_global = 0.0f;
    float pico_menor = 0.0f, pico_mayor = 0.0f;
    double suma_picos = 0.0;
    uint32_t n_picos = 0;

    printf("--> Umbral de detección de picos: %.1f cuentas\r\n", umbral_alto);

    IniciarMedicion();

    for (uint32_t i = 0; i < MUESTRAS_CALIB; i++) {
        EsperarTick();
        uint16_t raw = 0;
        float filtrada = LeerMuestraFiltrada(&raw);
        float s = (float)signo * (filtrada - (float)offset_v);

        if (s > extremo_global) extremo_global = s;

        if (!en_pico) {
            if (s > umbral_alto) {
                en_pico = true;
                pico_actual = s;
            }
        } else {
            if (s > pico_actual) pico_actual = s;
            if (s < umbral_bajo) {
                if (n_picos == 0 || pico_actual < pico_menor) pico_menor = pico_actual;
                if (n_picos == 0 || pico_actual > pico_mayor) pico_mayor = pico_actual;
                suma_picos += (double)pico_actual;
                n_picos++;
                en_pico = false;
            }
        }

        if (((i + 1) % SAMPLE_FREQ) == 0) {
            printf(" Segundos: %lu/%d (ADC crudo: %u | picos: %lu | promedio: %.1f)\r\n",
                   (unsigned long)((i + 1) / SAMPLE_FREQ), CALIB_SEC, (unsigned)raw,
                   (unsigned long)n_picos, (n_picos > 0) ? (suma_picos / n_picos) : 0.0);
        }
    }

    DetenerMuestreo();

    float promedio;
    if (n_picos >= PICOS_MIN_VALIDOS) {
        promedio = (float)(suma_picos / (double)n_picos);
        printf("--> %s: %lu picos | menor: %.1f | mayor: %.1f | PROMEDIO: %.1f cuentas\r\n",
               etiqueta, (unsigned long)n_picos, pico_menor, pico_mayor, promedio);
    } else {
        promedio = extremo_global;
        printf("!! ADVERTENCIA (%s): solo %lu picos detectados (mínimo %d). "
               "Se usa el máximo absoluto (%.1f). Repita con movimientos más marcados.\r\n",
               etiqueta, (unsigned long)n_picos, PICOS_MIN_VALIDOS, promedio);
    }

    return (int16_t)((float)signo * promedio);
}

/**
 * @brief Rutina secuencial de calibración guiada (4 etapas de CALIB_SEC s a 1 kHz).
 */
static void CalibrarEOG(void) {
    calibracion_completada = false;

    // --- 1: REPOSO ---
    printf("\r\n==========================================\r\n");
    printf("=== Paso 1/4: Calibración de REPOSO ===\r\n");
    printf("Mire hacia el frente en reposo y presione SWITCH_1 para medir durante %d segundos...\r\n", CALIB_SEC);
    EsperarSwitch1();
    printf("--> Midiendo reposo...\r\n");
    offset_v = MedirReposo();
    printf("--> Finalizó Calibración 1. Offset de reposo: %d cuentas\r\n", offset_v);

    // --- 2: MIRADA ARRIBA ---
    printf("\r\n==========================================\r\n");
    printf("=== Paso 2/4: Calibración MIRADA ARRIBA ===\r\n");
    printf("Mire hacia ARRIBA y vuelva al centro repetidamente (con pausas breves en el centro).\r\n");
    printf("Presione SWITCH_1 para iniciar la medición de %d segundos...\r\n", CALIB_SEC);
    EsperarSwitch1();
    printf("--> Midiendo mirada arriba... ¡Realice los movimientos ahora!\r\n");
    umbral_arriba_ref = MedirPicos(+1, "Arriba");
    printf("--> Finalizó Calibración 2. Referencia Arriba (pico promedio): %d cuentas\r\n", umbral_arriba_ref);

    // --- 3: MIRADA ABAJO ---
    printf("\r\n==========================================\r\n");
    printf("=== Paso 3/4: Calibración MIRADA ABAJO ===\r\n");
    printf("Mire hacia ABAJO y vuelva al centro repetidamente (con pausas breves en el centro).\r\n");
    printf("Presione SWITCH_1 para iniciar la medición de %d segundos...\r\n", CALIB_SEC);
    EsperarSwitch1();
    printf("--> Midiendo mirada abajo... ¡Realice los movimientos ahora!\r\n");
    umbral_abajo_ref = MedirPicos(-1, "Abajo");
    printf("--> Finalizó Calibración 3. Referencia Abajo (pico promedio): %d cuentas\r\n", umbral_abajo_ref);

    // --- 4: PESTAÑEO ---
    printf("\r\n==========================================\r\n");
    printf("=== Paso 4/4: Calibración de PESTAÑEO ===\r\n");
    printf("Prepárese para realizar pestañeos firmes de forma repetida.\r\n");
    printf("Presione SWITCH_1 para iniciar la medición de %d segundos...\r\n", CALIB_SEC);
    EsperarSwitch1();
    printf("--> Midiendo pestañeo... ¡Pestañee firmemente ahora!\r\n");
    umbral_pestaneo_ref = MedirPicos(+1, "Pestañeo");
    printf("--> Finalizó Calibración 4. Referencia Pestañeo (pico promedio): %d cuentas\r\n", umbral_pestaneo_ref);

    // --- Validación básica ---
    if (umbral_arriba_ref <= 0 || umbral_abajo_ref >= 0 || umbral_pestaneo_ref <= 0) {
        printf("!! ADVERTENCIA: alguna referencia tiene signo inesperado (arriba>0, abajo<0, pestaneo>0).\r\n");
        printf("!! Revise electrodos/polaridad: los movimientos asociados no se detectarán.\r\n");
    }
    if (umbral_pestaneo_ref > 0 && (FRAC_PESTANEO * (float)umbral_pestaneo_ref) <= (float)umbral_arriba_ref) {
        printf("!! ADVERTENCIA: el pestañeo no supera claramente a la mirada arriba; "
               "mirar arriba podría generar clicks.\r\n");
    }

    // --- Bluetooth ---
    printf("\r\n==========================================\r\n");
    printf("¡Calibración completada con éxito!\r\n");
    printf("Offsets y Referencias -> Reposo: %d | Arriba: %d | Abajo: %d | Pestañeo: %d\r\n",
           offset_v, umbral_arriba_ref, umbral_abajo_ref, umbral_pestaneo_ref);
    printf("Iniciando servicio Bluetooth HID ('EOG_Mouse')...\r\n");

    BleHidInit("EOG_Mouse");

    printf(">> PASO 2: Vincule su computadora/móvil al Bluetooth 'EOG_Mouse'.\r\n");
    printf(">> PASO 3: Abra la interfaz web para visualizar las mediciones.\r\n");
    printf("==========================================\r\n\n");

    vTaskDelay(pdMS_TO_TICKS(100));
    calibracion_completada = true;
}

/** @brief Pasa al estado de bloqueo (espera a que la señal vuelva y se asiente en el centro). */
static void EntrarBloqueo(uint16_t *t_estado, uint16_t *t_banda) {
    estado_actual = ESTADO_BLOQUEO;
    *t_estado = 0;
    *t_banda = 0;
}

/**
 * @brief Máquina de estados que traduce la señal EOG centrada en movimiento/click de mouse.
 *        Se llama a 100 Hz (una vez por muestra decimada).
 */
static void UpdateMouse(int8_t *pos, uint8_t *click_flag, int16_t eog_centrado) {
    static uint16_t t_estado = 0;
    static uint16_t t_banda = 0;

    *click_flag = 0;
    *pos = 0;

    if (!calibracion_completada) return;

    const bool arriba_ok = (umbral_arriba_ref > 0);
    const bool abajo_ok = (umbral_abajo_ref < 0);
    const bool pestaneo_ok = (umbral_pestaneo_ref > 0);

    const float c = (float)eog_centrado;
    const float arriba_det = FRAC_DETECCION * (float)umbral_arriba_ref;
    const float arriba_ret = FRAC_RETORNO * (float)umbral_arriba_ref;
    const float abajo_det = FRAC_DETECCION * (float)umbral_abajo_ref;
    const float abajo_ret = FRAC_RETORNO * (float)umbral_abajo_ref;
    const float click_det = FRAC_PESTANEO * (float)umbral_pestaneo_ref;

    // Pestañeo -> click
    if (pestaneo_ok && estado_actual != ESTADO_BLOQUEO && c > click_det) {
        *click_flag = 1;
        EntrarBloqueo(&t_estado, &t_banda);
        return;
    }

    switch (estado_actual) {
        case ESTADO_CENTRO:
            if (arriba_ok && c >= arriba_det) {
                estado_actual = ESTADO_ESPERANDO_RETORNO_ARRIBA;
                t_estado = 0;
            } else if (abajo_ok && c <= abajo_det) {
                estado_actual = ESTADO_ESPERANDO_RETORNO_ABAJO;
                t_estado = 0;
            }
            break;

        case ESTADO_ESPERANDO_RETORNO_ARRIBA:
            t_estado++;
            if (c < arriba_ret) {
                *pos = -PASO_MOUSE_PX;
                EntrarBloqueo(&t_estado, &t_banda);
            } else if (t_estado > ESPERA_RETORNO_MAX) {
                EntrarBloqueo(&t_estado, &t_banda);
            }
            break;

        case ESTADO_ESPERANDO_RETORNO_ABAJO:
            t_estado++;
            if (c > abajo_ret) {
                *pos = PASO_MOUSE_PX;
                EntrarBloqueo(&t_estado, &t_banda);
            } else if (t_estado > ESPERA_RETORNO_MAX) {
                EntrarBloqueo(&t_estado, &t_banda);
            }
            break;

        case ESTADO_BLOQUEO: {
            const float banda_sup = arriba_ok ? arriba_ret : UMBRAL_DET_MIN_CUENTAS;
            const float banda_inf = abajo_ok ? abajo_ret : -UMBRAL_DET_MIN_CUENTAS;

            t_estado++;
            if (c < banda_sup && c > banda_inf) {
                t_banda++;
            } else {
                t_banda = 0;
            }

            if (t_banda >= REARME_MUESTRAS || t_estado >= BLOQUEO_MAX_MUESTRAS) {
                estado_actual = ESTADO_CENTRO;
            }
            break;
        }
    }
}

/*==================[Tasks]==================================================*/

/** @brief Antirrebote simple de SWITCH_1; libera un semáforo en cada pulsación. */
void SwitchTask(void *pvParameter) {
    while (true) {
        if (SwitchesRead() & SWITCH_1) {
            xSemaphoreGive(sem_switch);
            do {
                vTaskDelay(pdMS_TO_TICKS(50));
            } while (SwitchesRead() & SWITCH_1);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/**
 * @brief Adquisición a 1 kHz (marcada por el timer). Calibra y luego envía muestras
 *        decimadas a 100 Hz por la cola.
 */
void AdquisicionTask(void *pvParameter) {
    uint8_t decimation_counter = 0;
    muestra_eog_t muestra;

    CalibrarEOG();
    IniciarMuestreo();

    while (true) {
        EsperarTick();

        uint16_t raw = 0;
        float filtrada = LeerMuestraFiltrada(&raw);
        int16_t centrado = (int16_t)(filtrada - (float)offset_v);

        if (++decimation_counter >= DECIMATION_FACTOR) {
            decimation_counter = 0;
            muestra.raw = raw;
            muestra.centrado = centrado;
            xQueueSend(cola_muestras, &muestra, 0);
        }
    }
}

/** @brief Procesa las muestras a 100 Hz y envía los eventos de mouse por BLE HID. */
void ProcesamientoOutputTask(void *pvParameter) {
    muestra_eog_t muestra;
    int8_t mouse_y = 0;
    uint8_t mouse_click = 0;

    while (true) {
        if (xQueueReceive(cola_muestras, &muestra, portMAX_DELAY) == pdTRUE) {
            UpdateMouse(&mouse_y, &mouse_click, muestra.centrado);

            if (mouse_click) {
                BleHidSendMouse(HID_MOUSE_LEFT, 0, 0);
                vTaskDelay(pdMS_TO_TICKS(CLICK_PULSO_MS));
                BleHidSendMouse(0, 0, 0);
            } else if (mouse_y != 0) {
                BleHidSendMouse(0, 0, mouse_y);
            }
            printf("%d,%d,%u\r\n", muestra.centrado, mouse_y, mouse_click);
        }
    }
}

/*==================[external functions definition]==========================*/

void app_main(void) {
    SwitchesInit();

    adc_eog_v.input = CH3;
    adc_eog_v.mode = ADC_SINGLE;
    AnalogInputInit(&adc_eog_v);

    LowPassInit(SAMPLE_FREQ, 28, ORDER_2);

    sem_switch = xSemaphoreCreateBinary();
    cola_muestras = xQueueCreate(QUEUE_LEN, sizeof(muestra_eog_t));

    timer_config_t timer_muestreo = {
        .timer = TIMER_A,
        .period = SAMPLE_PERIOD_US,
        .func_p = TimerMuestreoISR,
        .param_p = NULL
    };
    TimerInit(&timer_muestreo);

    xTaskCreate(&AdquisicionTask, "ADQUISICION_EOG", 8192, NULL, 5, &adquisicion_task_handle);
    xTaskCreate(&ProcesamientoOutputTask, "PROCESAMIENTO_MOUSE", 4096, NULL, 3, NULL);
    xTaskCreate(&SwitchTask, "TECLA_DESBLOQUEO", 2048, NULL, 2, NULL);
}