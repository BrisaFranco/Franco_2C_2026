/*! @mainpage Template
 *
 * @section genDesc Descripción general
 *
 * @section hardConn Conexión de hardware
 *
 * @section changelog Historial de cambios
 *
 * @author Brisa Franco (brisa.franco@ingenieria.uner.edu.ar)
 *
 */

/*==================[inclusiones]=============================================*/
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gpio_mcu.h"

/*==================[macros y definiciones]==================================*/

#define NUMERO_A_MOSTRAR    121
#define CANT_DIGITOS        3

/*==================[definición de datos internos]============================*/

/* Estructura que contiene la configuración de un GPIO */
typedef struct
{
    gpio_t pin;         /* Número de GPIO */
    io_t dir;           /* Dirección del GPIO */
} gpioConf_t;


/*
 * Vector correspondiente a los cuatro bits del bus BCD.
 * GPIO_20 corresponde a D1, GPIO_21 a D2, GPIO_22 a D3 y GPIO_23 a D4.
 */
gpioConf_t vectorGPIO[4] =
{
    {GPIO_20, GPIO_OUTPUT},
    {GPIO_21, GPIO_OUTPUT},
    {GPIO_22, GPIO_OUTPUT},
    {GPIO_23, GPIO_OUTPUT}
};

/*
 * Vector correspondiente a las señales de selección.
 * GPIO_9 corresponde a SEL_3 (Unidades), GPIO_18 a SEL_2 (Decenas), GPIO_19 a SEL_1 (Centenas).
 */
gpioConf_t vectorSEL[3] =
{
    {GPIO_9, GPIO_OUTPUT},
    {GPIO_18, GPIO_OUTPUT},
    {GPIO_19, GPIO_OUTPUT}
};

/*==================[declaración de funciones internas]======================*/

/*
 * Convierte un número decimal en un arreglo de dígitos BCD.
 * Obtiene los dígitos del número comenzando por el menos significativo.
 */
int8_t convertToBcdArray(uint16_t data, uint8_t digits, uint8_t *bcd_number);

/*
 * Escribe un dígito BCD en el bus de cuatro bits.
 */
void escribirGPIOdesdeBCD(uint8_t digitoBCD, gpioConf_t *vectorGPIO);

/*
 * Muestra un número en el LCD-EDU-ESP.
 * Convierte el número recibido a BCD y almacena cada dígito
 * en el registro interno correspondiente del CD4543 mediante
 * un pulso en la señal SEL.
 */
void mostrarNumero(uint16_t numero, uint8_t cantidadDigitos, gpioConf_t *vectorBCD,
                   gpioConf_t *vectorSEL);


/*==================[definición de funciones externas]=======================*/

int8_t convertToBcdArray(uint16_t data,
                         uint8_t digits,
                         uint8_t *bcd_number)
{
    uint8_t i;

    /* Obtengo cada dígito comenzando por las unidades */
    for (i = 0; i < digits; i++)
    {
        bcd_number[i] = data % 10;
        data = data / 10;
    }

    return 0;
}


void escribirGPIOdesdeBCD(uint8_t digitoBCD,
                          gpioConf_t *vectorGPIO)
{
    uint8_t b;

    /*
     * Recorro los cuatro bits del dígito BCD y
     * escribo cada uno en el GPIO correspondiente.
     */
    for (b = 0; b < 4; b++)
    {
        uint8_t valorBit = (digitoBCD >> b) & 0x01;

        GPIOState(vectorGPIO[b].pin, valorBit);
    }
}


void mostrarNumero(uint16_t numero,
                   uint8_t cantidadDigitos,
                   gpioConf_t *vectorBCD,
                   gpioConf_t *vectorSEL)
{
    uint8_t digitos[cantidadDigitos];
    uint8_t i;

    /*
     * Inicializo los cuatro GPIO correspondientes
     * al bus BCD.
     */
    for (i = 0; i < 4; i++)
    {
        GPIOInit(vectorBCD[i].pin, vectorBCD[i].dir);
    }

    /*
     * Inicializo los GPIO correspondientes a las
     * señales de selección de los displays.
     */
    for (i = 0; i < cantidadDigitos; i++)
    {
        GPIOInit(vectorSEL[i].pin, vectorSEL[i].dir);
        GPIOState(vectorSEL[i].pin, 0);
    }

    /*
     * Convierto el número recibido a un arreglo de
     * dígitos BCD.
     */
    convertToBcdArray(numero, cantidadDigitos, &digitos[0]);

    /*
     * Recorro cada uno de los dígitos.
     *
     * Primero coloco el dígito en el bus BCD y luego
     * genero un pulso en la señal SEL correspondiente.
     *
     * El CD4543 almacena internamente el valor BCD
     * presente en el bus cuando recibe el pulso de SEL.
     */
    for (i = 0; i < cantidadDigitos; i++)
    {
        escribirGPIOdesdeBCD(digitos[i], vectorBCD);

        /* Pulso de selección para almacenar el dígito */
        GPIOState(vectorSEL[i].pin, 1);
        GPIOState(vectorSEL[i].pin, 0);
    }
}

/*==================[función principal]======================================*/

void app_main(void)
{
    /* Muestro el número recibido en el LCD utilizando las macros definidas */
    mostrarNumero(NUMERO_A_MOSTRAR,
                  CANT_DIGITOS,
                  vectorGPIO,
                  vectorSEL);
}