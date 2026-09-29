# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 1.5.1)
set(toolchainVersion 13_2_Rel1)
set(picotoolVersion 2.0.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
# Target hardware: "gcp2" is the Hand Held Legend GC Pocket+ PCB,
# "rp2040_zero" is a Waveshare RP2040-Zero (or clone) wired by hand.
set(ADAPTER_BOARD "gcp2" CACHE STRING "Target hardware: gcp2 or rp2040_zero")
set_property(CACHE ADAPTER_BOARD PROPERTY STRINGS gcp2 rp2040_zero)

if(ADAPTER_BOARD STREQUAL "rp2040_zero")
  set(PICO_BOARD waveshare_rp2040_zero CACHE STRING "Board type")
  # First of the four consecutive GPIOs used for GC ports 1-4
  set(ADAPTER_GC_PIN_BASE 26 CACHE STRING "First GC data GPIO (ports use consecutive pins from here)")
  set(ADAPTER_PORT_COUNT 4 CACHE STRING "Number of GC ports, 1-4")
  option(ADAPTER_LED_RGB_ORDER "Onboard LED takes RGB instead of GRB byte order" OFF)
elseif(ADAPTER_BOARD STREQUAL "gcp2")
  set(PICO_BOARD pico CACHE STRING "Board type")
else()
  message(FATAL_ERROR "Unknown ADAPTER_BOARD '${ADAPTER_BOARD}' (use gcp2 or rp2040_zero)")
endif()

# Clone boards ship with assorted flash chips. The generic boot stage 2 works
# with any SPI NOR flash, at the cost of slower code fetches from flash.
option(ADAPTER_GENERIC_FLASH_BOOT "Use the generic (03h) flash boot stage 2" OFF)
if(ADAPTER_GENERIC_FLASH_BOOT)
  set(PICO_DEFAULT_BOOT_STAGE2 boot2_generic_03h)
endif()

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(gc_adapter_rp2040 C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

include_directories(
  include
  common/include
  common/include/devices
  common/include/generated
  common/include/switch
  common/include/usb
  common/include/utilities
  common/include/ll
)

# Add executable. Default name is the project name, version 0.1

add_executable(${PROJECT_NAME}
  src/main.c 
  src/joybus_itf.c 
  src/rgb_itf.c 

  common/utilities/rgb.c 
  common/utilities/joybus.c 
  common/utilities/interval.c

  common/ll/adapter_ll_rp2040.c 

  common/usb/desc_bos.c 
  
  common/adapter.c
  common/adapter_tusb.c 

  common/adapter_defines.c 
  common/adapter_settings.c 

  common/usb/xinput_driver.c 
  common/usb/ginput_driver.c

  common/switch/switch_analog.c 
  common/switch/switch_commands.c 
  common/switch/switch_spi.c 

  common/devices/gcinput.c 
  common/devices/swpro.c 
  common/devices/webusb.c 
  common/devices/xinput.c
)

pico_set_program_name(${PROJECT_NAME} "${PROJECT_NAME}")
pico_set_program_version(${PROJECT_NAME} "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(${PROJECT_NAME} 0)
pico_enable_stdio_usb(${PROJECT_NAME} 0)

file(MAKE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/include/generated)
pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/pio/ws2812.pio OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/include/generated)
pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/pio/joybus.pio OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/include/generated)

target_compile_definitions(${PROJECT_NAME}
	PRIVATE
	PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
	PICO_DEFAULT_UART_TX_PIN=12
	PICO_DEFAULT_UART_RX_PIN=13
    ADAPTER_MCU_TYPE=1
	#PICO_FLASH_SPI_CLKDIV=8
	)

if(ADAPTER_BOARD STREQUAL "rp2040_zero")
  target_compile_definitions(${PROJECT_NAME} PRIVATE
    ADAPTER_BOARD_RP2040_ZERO=1
    ADAPTER_GC_PIN_BASE=${ADAPTER_GC_PIN_BASE}
    ADAPTER_PORT_COUNT=${ADAPTER_PORT_COUNT}
  )
  if(ADAPTER_LED_RGB_ORDER)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ADAPTER_LED_RGB_ORDER=1)
  endif()
endif()

# Pull in pico libraries that we need
target_link_libraries(${PROJECT_NAME}
  PRIVATE
  pico_stdlib
  pico_bootrom
  pico_multicore
  pico_platform
  pico_rand

  hardware_gpio
  hardware_adc
  hardware_flash
  hardware_pio
  hardware_dma
  hardware_pwm
  hardware_spi
  hardware_sync
  hardware_timer
  hardware_irq

  tinyusb_device
  tinyusb_board
)

# Add the standard include files to the build
target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

pico_add_extra_outputs(${PROJECT_NAME})

add_custom_command(TARGET ${PROJECT_NAME}
 					POST_BUILD
					COMMAND ${CMAKE_COMMAND} 
					-DTARGET_NAME=${PROJECT_NAME}
					-DTARGET_PATH=${CMAKE_CURRENT_SOURCE_DIR}
					-P ${CMAKE_SOURCE_DIR}/manifest.cmake
					COMMENT "Updating version in manifest.json"
					WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
					)
