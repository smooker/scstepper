#!/bin/bash
cd "$(dirname "$0")/.."
echo "#include \"./Core/Inc/main.h\"" | arm-none-eabi-gcc -E -dM - -c \
    -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
    -DUSE_HAL_DRIVER -DSTM32F411xE \
    -ICore/Inc \
    -IDrivers/STM32F4xx_HAL_Driver/Inc \
    -IDrivers/STM32F4xx_HAL_Driver/Inc/Legacy \
    -IDrivers/CMSIS/Device/ST/STM32F4xx/Include \
    -IDrivers/CMSIS/Include \
    -IUSB_DEVICE/App \
    -IUSB_DEVICE/Target \
    -IMiddlewares/ST/STM32_USB_Device_Library/Core/Inc \
    -IMiddlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc \
    > ./Core/Inc/defines.h
