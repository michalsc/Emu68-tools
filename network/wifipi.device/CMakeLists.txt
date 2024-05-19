cmake_minimum_required(VERSION 3.14.0)
project(WiFiPi VERSION 0.3.4)
get_verstring(VERSTRING)

add_link_options(-ffreestanding -nostdlib -nostartfiles -Wl,-e,__start)
add_compile_options(-Os -m68040 -Wall -Wextra -Werror -fomit-frame-pointer)
add_compile_definitions(PRIVATE VERSION_STRING="${VERSTRING}")

add_executable(wifipi.device
    src/start.c
    src/device.c
    src/init.c
    src/mbox.c
    src/sdio.c
    src/end.c
    src/wifipi.c
    src/packet.c
    src/d11.c
    src/unit.c
    src/findtoken.c
)
target_include_directories(wifipi.device PRIVATE include)
target_link_libraries(wifipi.device devicetree amiga sana2 common)

add_executable(wifipi_rom
    src/start.c
    src/device.c
    src/init.c
    src/mbox.c
    src/sdio.c
    src/end.c
    src/wifipi.c
    src/packet.c
    src/d11.c
    src/unit.c
    src/findtoken.c
)

target_include_directories(wifipi_rom PRIVATE include)
target_link_libraries(wifipi_rom devicetree sana2 common)
target_link_options(wifipi_rom PRIVATE -Wl,-e_rom_start -Wl,-T${CMAKE_SOURCE_DIR}/extrom.lds)
add_custom_command(
    TARGET wifipi_rom POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -v -O binary "${CMAKE_CURRENT_BINARY_DIR}/wifipi_rom" "${CMAKE_CURRENT_BINARY_DIR}/wifipi.rom"
    COMMAND dd if="${CMAKE_CURRENT_BINARY_DIR}/wifipi.rom" of="${CMAKE_CURRENT_BINARY_DIR}/wifipi.rom~" bs=512k skip=30
    COMMAND mv "${CMAKE_CURRENT_BINARY_DIR}/wifipi.rom~" "${CMAKE_CURRENT_BINARY_DIR}/wifipi.rom"
    #COMMAND cp "${CMAKE_CURRENT_BINARY_DIR}/wifipi.rom" /home/michal/tftpboot/
    COMMAND rm "${CMAKE_CURRENT_BINARY_DIR}/wifipi_rom"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
set_target_properties(wifipi_rom PROPERTIES LINK_DEPENDS ${CMAKE_SOURCE_DIR}/extrom.lds)

install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/wifipi.device DESTINATION ./Emu68-WiFi/Devs/Networks)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/firmware/ DESTINATION ./Emu68-WiFi/Devs/Firmware)
