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
    Core/Src/display/display_backlight.c
    Core/Src/display/display_compose_helpers.c
    Core/Src/display/display_palette_registry.c
    Core/Src/display/display_theme.c
    Core/Src/display/display_main_title.c
    Core/Src/display/display_menu_controller.c
    Core/Src/display/display_menu_pages.c
    Core/Src/display/display_menu_row_render.c
    Core/Src/display/display_menu_redraw.c
    Core/Src/display/display_menu_redraw_utils.c
    Core/Src/display/display_menu_page_devices.c
    Core/Src/display/display_menu_page_device_edit.c
    Core/Src/display/display_menu_page_bank_edit.c
    Core/Src/display/display_menu_page_global.c
    Core/Src/display/display_menu_page_midi_monitor.c
    Core/Src/display/display_menu_page_user_theme.c
    Core/Src/display/display_menu_page_banks.c
    Core/Src/display/display_menu_page_function_button_text.c
    Core/Src/display/display_menu_page_function_button_compare.c
    Core/Src/display/display_menu_page_function_button_redraw.c
    Core/Src/display/display_menu_text_edit.c
    Core/Src/display/display_menu_value.c
    Core/Src/display/display_value_helpers.c
    Core/Src/display/display_loading_bar.c
    Core/Src/display/display_status_strip.c
    Core/Src/display/display_screensaver.c
    Core/Src/display_functions.c
    Core/Src/bpm_functions.c
    Core/Src/led_functions.c
    Core/Src/midi/midi_monitor.c
    Core/Src/midi/midi_output.c
    Core/Src/midi_functions.c
    Core/Src/midi_devices.c
    Core/Src/presets.c
    User/Src/app/app_activation.c
    User/Src/app/app_board_init.c
    User/Src/app/app_button_combo.c
    User/Src/app/app_button_events.c
    User/Src/app/app_button_monitor.c
    User/Src/app/app_encoder_sampler.c
    User/Src/app/app_encoder_math.c
    User/Src/app/app_footswitch_input.c
    User/Src/app/app_input_encoder_switches.c
    User/Src/app/app_input_board.c
    User/Src/app/app_input_irq.c
    User/Src/app/app_input_sampling.c
    User/Src/app/app_requests.c
    User/Src/app/app_dispatch.c
    User/Src/app/app_input.c
    User/Src/app/app_save_service.c
    User/Src/app/app_special_functions.c
    User/Src/app/app_state.c
    User/Src/app/app_runtime.c
    User/Src/app/app_startup.c
    User/Src/app/app_startup_bootstrap.c
    User/Src/app/app_startup_clock.c
    User/Src/app/app_startup_display.c
    User/Src/app/app_tempo.c
    User/Src/app/app_ui.c
    User/Src/app/app_ui_menu.c
    User/Src/app/app_ui_input_events.c
    User/Src/app/app_ui_render.c
    User/Src/app/app_ui_runtime_events.c
    User/Src/app_event.c
    User/Src/runtime_config.c
    # ST7796 display driver
    Drivers/ST7796/st7796.c
    Drivers/ST7796/fonts.c
    Drivers/ST7796/font_consolas_8x21.c
    Drivers/ST7796/font_consolas_15x35.c
    Drivers/ST7796/font_consolas_23x49.c
    Drivers/ST7796/font_consolas_24x40.c
    Drivers/ST7796/font_c64_8x21.c
    Drivers/ST7796/font_c64_15x35.c
    Drivers/ST7796/font_c64_23x49.c
    Drivers/ST7796/font_bios_8x21.c
    Drivers/ST7796/font_bios_15x35.c
    Drivers/ST7796/font_bios_23x49.c
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
