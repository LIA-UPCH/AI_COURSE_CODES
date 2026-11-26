# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

set(PICO_BOARD pico2_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(Test_regression02 C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add TensorFlow Lite for Microcontrollers
add_subdirectory(pico-tflmicro EXCLUDE_FROM_ALL)

# Fix GCC 13/14/15 missing <cstdint> in pico-tflmicro's bundled flatbuffers
target_compile_options(pico-tflmicro PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:SHELL:-include cstdint>
)

# Add executable
add_executable(${PROJECT_NAME}
    Test_regression02.cpp
)

pico_set_program_name(${PROJECT_NAME} "Test_regression02")
pico_set_program_version(${PROJECT_NAME} "0.1")

# Enable USB stdio, disable UART stdio
pico_enable_stdio_uart(${PROJECT_NAME} 0)
pico_enable_stdio_usb(${PROJECT_NAME} 1)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

# Link Pico SDK libraries + TFLite Micro
target_link_libraries(${PROJECT_NAME}
    pico_stdlib
    hardware_i2c
    hardware_dma
    pico-tflmicro
)

# Generate .uf2, .bin, .hex, and .dis files
pico_add_extra_outputs(${PROJECT_NAME})