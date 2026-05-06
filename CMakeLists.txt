cmake_minimum_required(VERSION 3.22)

# ===================== CubeMX-Proofing Notice =====================
# This project is structured to be robust against STM32CubeMX regenerating files.
#
# - All custom sources, drivers, and includes are referenced ONLY in this top-level CMakeLists.txt.
# - CubeMX-generated files (HAL, startup, Core/Inc, Core/Src) are managed in cmake/stm32cubemx/CMakeLists.txt.
# - NEVER add custom files or drivers to cmake/stm32cubemx/CMakeLists.txt.
# - Keep all custom code outside Core/ (e.g., Drivers/, User/), so CubeMX cannot overwrite or delete them.
# - After CubeMX regeneration, review git changes to ensure nothing critical was lost.
#
# You CAN safely use CubeMX to reconfigure peripherals or regenerate code.
# After regeneration, simply:
#   1. Check that your custom files in Drivers/ (or other custom folders) are untouched.
#   2. Confirm this CMakeLists.txt still lists all your custom sources and includes.
#   3. If CubeMX adds/removes HAL files, review cmake/stm32cubemx/CMakeLists.txt and update if needed.
#   4. Use git to restore any accidentally deleted files.
# ==================================================================

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME ChatTest)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM, C and C++ languages
enable_language(C CXX ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Custom application sources
    Core/Src/display_compose.c
    Core/Src/display_functions.c
    Core/Src/bpm_functions.c
    Core/Src/led_functions.c
    Core/Src/midi_functions.c
    Core/Src/midi_devices.c
    Core/Src/presets.c
    User/Src/runtime_config.c
    # ST7796 display driver
    Drivers/ST7796/st7796.c
    Drivers/ST7796/fonts.c
    Drivers/ST7796/font_consolas_8x21.c
    Drivers/ST7796/font_consolas_15x35.c
    Drivers/ST7796/font_consolas_23x49.c
    Drivers/ST7796/font_consolas_24x40.c
    Core/Src/button_functions.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # ST7796 driver headers
    Drivers/ST7796
    User/Inc
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
