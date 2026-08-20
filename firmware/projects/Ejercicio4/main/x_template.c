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
 * |    Peripheral  |   ESP32   	|
 * |:--------------:|:--------------|
 * | 	PIN_X	 	| 	GPIO_X		|
 *
 *
 * @section changelog Changelog
 *
 * |   Date	    | Description                                    |
 * |:----------:|:-----------------------------------------------|
 * | 12/09/2023 | Document creation		                         |
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
#include "gpio_mcu.h"
/*==================[macros and definitions]=================================*/

/*==================[internal data definition]===============================*/
int8_t convertToBcdArray(uint16_t data, uint8_t digits, uint8_t *bcd_number); //declaro la función 
/*==================[internal functions declaration]=========================*/
typedef struct
{
    gpio_t pin;         /*!< GPIO pin number */
    io_t dir;           /*!< GPIO direction '0' IN; '1' OUT*/
} gpioConf_t;

gpioConf_t vectorGPIO[4] =
{
    {GPIO_20, GPIO_OUTPUT},
    {GPIO_21, GPIO_OUTPUT},
    {GPIO_22, GPIO_OUTPUT},
    {GPIO_23, GPIO_OUTPUT}
};

gpioConf_t vectorSEL[3] =
{
    {GPIO_19, GPIO_OUTPUT},
    {GPIO_18, GPIO_OUTPUT},
    {GPIO_9, GPIO_OUTPUT}
};
/*==================[external functions definition]==========================*/
int8_t convertToBcdArray(uint16_t data, uint8_t digits, uint8_t *bcd_number)
{
    uint8_t i;

    for (i = 0; i < digits; i++)
    {
        bcd_number[i] = data % 10;
        data = data / 10;
    }

    return 0;
}

void escribirGPIOdesdeBCD(uint8_t digitoBCD, gpioConf_t *vectorGPIO)
{
    for (int b = 0; b < 4; b++)
    {
        uint8_t valorBit = (digitoBCD >> b) & 0x01;

        GPIOInit(vectorGPIO[b].pin, vectorGPIO[b].dir);

        GPIOState(vectorGPIO[b].pin, valorBit);
    }
}

void mostrarNumero(uint16_t numero, uint8_t cantidadDigitos,
                   gpioConf_t *vectorBCD, gpioConf_t *vectorSEL)
{
    uint8_t digitos[cantidadDigitos];

    convertToBcdArray(numero, cantidadDigitos, &digitos[0]);

    for (int i = 0; i < cantidadDigitos; i++)
    {
        escribirGPIOdesdeBCD(digitos[i], vectorBCD);

        GPIOState(vectorSEL[i].pin, 1);
        GPIOState(vectorSEL[i].pin, 0);
    }
}

void app_main(void)
{
    uint16_t numero = 123;
    uint8_t cantidadDigitos = 3;

    mostrarNumero(numero, cantidadDigitos, vectorGPIO, vectorSEL);
}


/*==================[end of file]============================================*/