#!/bin/bash
echo '[General]' > stepper.creator
find . -name "*.c" > stepper.files
find . -name "*.h" >> stepper.files
echo "Makefile" >> stepper.files
echo "./Core/Inc/" > stepper.includes
echo "./Drivers/CMSIS/Include" >> stepper.includes
echo "./USB_DEVICE/App" >> stepper.includes
echo "./USB_DEVICE/Target" >> stepper.includes
echo "./Middlewares/ST/STM32_USB_Device_Library/Core/Inc" >> stepper.includes
echo "./Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc" >> stepper.includes

#find . -name stm32f4xx_hal.h
#find . -name stm32f4xx.h

