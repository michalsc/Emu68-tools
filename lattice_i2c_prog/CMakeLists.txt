cmake_minimum_required(VERSION 3.14.0)
project(LatticeI2cProg VERSION 1.0.0 LANGUAGES C CXX)
get_verstring(VERSTRING)

add_link_options(-m68020 -noixemul)
add_compile_options(-O3 -m68020 -fomit-frame-pointer)
add_compile_definitions(PRIVATE VERSION_STRING="${VERSTRING}")

file(GLOB i2c_updater_sources src/*.cpp src/*.hpp)
add_executable(LatticeI2cProg ${i2c_updater_sources})

target_link_libraries(LatticeI2cProg common amiga i2c)

install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/LatticeI2cProg DESTINATION ./LatticeI2cProg/)
#install(FILES LatticeI2cProg.info DESTINATION ./LatticeI2cProg/)
