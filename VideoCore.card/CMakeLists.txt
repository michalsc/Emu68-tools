cmake_minimum_required(VERSION 3.14.0)
project(VideoCore VERSION 1.1.0)
get_verstring(VERSTRING)

add_link_options(-ffreestanding -nostdlib -nostartfiles -Wl,-e,__start)
add_compile_options(-Os -m68040 -fomit-frame-pointer)
add_compile_definitions(PRIVATE VERSION_STRING="${VERSTRING}")

add_library(VideoCore INTERFACE)
target_include_directories(VideoCore INTERFACE src)

add_executable(VideoCore.card
    src/main.c
    src/mbox.c
    src/vc4.c
    src/vc6.c
    src/support.c
    src/debug.c
    src/end.c
    src/vpu.c
)
target_link_libraries(VideoCore.card devicetree amiga)
target_compile_definitions(VideoCore.card PUBLIC CARD_NAME="VideoCore.card")

add_executable(Emu68-VC4.card
    src/main.c
    src/mbox.c
    src/vc4.c
    src/vc6.c
    src/support.c
    src/debug.c
    src/end.c
    src/vpu.c
)
target_link_libraries(Emu68-VC4.card devicetree amiga)
target_compile_definitions(Emu68-VC4.card PUBLIC CARD_NAME="Emu68-VC4.card")

install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/VideoCore.card DESTINATION ./VideoCore/)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/Emu68-VC4.card DESTINATION ./VideoCore/)
