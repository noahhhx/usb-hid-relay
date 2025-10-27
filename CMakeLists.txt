cmake_minimum_required(VERSION 4.0)
project(usb_hid_relay C)

set(CMAKE_C_STANDARD 11)

add_executable(usb_host usb_host.c)
add_executable(usb_client, usb_client.c)

# Link pthread library for threading support
target_link_libraries(usb_host pthread)