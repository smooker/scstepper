#!/bin/bash
cd "$(dirname "$0")/.."
echo '[General]' > scstepper.creator
find . -name "*.c" > scstepper.files
find . -name "*.h" >> scstepper.files
echo "Makefile" >> scstepper.files
echo "./Core/Inc/" > scstepper.includes
echo "./Drivers/CMSIS/Include" >> scstepper.includes
echo "./USB_DEVICE/App" >> scstepper.includes
echo "./USB_DEVICE/Target" >> scstepper.includes
echo "./Middlewares/ST/STM32_USB_Device_Library/Core/Inc" >> scstepper.includes
echo "./Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc" >> scstepper.includes
