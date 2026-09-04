cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(groovebox_panel C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(panel
  src/main.c
  src/usb_descriptors.c
  src/midi.c
  src/keys.c
  src/ads1115.c
  src/encoder.c
  src/leds.c
  src/config.c
)

target_include_directories(panel PRIVATE src)

target_link_libraries(panel PRIVATE
  pico_stdlib
  hardware_i2c
  hardware_flash
  hardware_watchdog
  tinyusb_device
)

# USB is owned by TinyUSB MIDI; no stdio channels (GP0/1 are encoders).
pico_enable_stdio_usb(panel 0)
pico_enable_stdio_uart(panel 0)

pico_add_extra_outputs(panel)   # .uf2 among others
