cmake_minimum_required(VERSION 3.14.0)
project(Buddabrot VERSION 1.0.0)
get_verstring(VERSTRING)

add_link_options(-m68040 -m68881 -noixemul)
add_compile_options(-O3 -m68040 -ffast-math -m68881 -fomit-frame-pointer)
add_compile_definitions(PRIVATE VERSION_STRING="${VERSTRING}")

add_executable(Buddhabrot
    src/main.c
    src/support.c
    src/master.c
    src/worker.c
)

set_target_properties(Buddhabrot PROPERTIES LINKER_LANGUAGE C)
target_link_libraries(Buddhabrot amiga m)

install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/Buddhabrot DESTINATION ./Buddhabrot/)
