cmake_minimum_required(VERSION 3.14.0)
project(EmuControl VERSION 1.5.0)
get_verstring(VERSTRING)

add_link_options(-ffreestanding -m68040 -nostdlib -nostartfiles -Wl,-e,__start)
add_compile_options(-Os -m68040 -m68881 -fomit-frame-pointer)
add_compile_definitions(PRIVATE VERSION_STRING="${VERSTRING}")

add_executable(EmuControl
    src/main.c
    src/mbox.c
    src/presets.c
)

target_link_libraries(EmuControl mui amiga devicetree common)

install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/EmuControl DESTINATION EmuControl/)
install(FILES EmuControl.info DESTINATION EmuControl/)
