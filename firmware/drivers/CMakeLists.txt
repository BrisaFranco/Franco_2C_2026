idf_build_get_property(target IDF_TARGET)

set(srcs
    "microcontroller/src/gpio_mcu.c"
    "microcontroller/src/delay_mcu.c"
    "microcontroller/src/timer_mcu.c"
    "microcontroller/src/uart_mcu.c"
    "microcontroller/src/gpio_fast_out_mcu.c"
    "microcontroller/src/analog_io_mcu.c"
    "microcontroller/src/ble_hid_mcu.c"       
    "devices/src/led.c"
    "devices/src/switch.c"
    "devices/src/lcditse0803.c"
    "devices/src/hc_sr04.c"
    "devices/src/ws2812b.c"
    "devices/src/neopixel_stripe.c"
)

set(includes 
    "microcontroller/inc"
    "devices/inc"
)

# Registro del componente
idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS ${includes}
    REQUIRES 
        driver 
        esp_adc 
        nvs_flash 
        bt
    PRIV_REQUIRES 
        esp_driver_gpio 
        esp_driver_uart 
        esp_driver_gptimer 
        esp_driver_sdm
)

if(TARGET __idf_bt)
    target_link_libraries(${COMPONENT_LIB} PUBLIC __idf_bt)
endif()