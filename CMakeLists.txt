cmake_minimum_required(VERSION 3.14.0)
project(Emu68-tools VERSION 1.1.0)

add_subdirectory(devicetree.resource)
add_subdirectory(mailbox.resource)
add_subdirectory(unicam.resource)
add_subdirectory(i2c.library)
add_subdirectory(SetClockI2C)
add_subdirectory(EmuControl)
add_subdirectory(VideoCore.card)
add_subdirectory(WiFiPi.device)
add_subdirectory(BinaryContribs)
add_subdirectory(Emu68Contribs)

add_custom_target(Emu68-tools ALL
    DEPENDS
        devicetree.resource
        mailbox.resource
        unicam.resource
        i2c.library
        SetClockI2C
        VideoCore.card
        WiFiPi.device
        EmuControl
)

configure_file(Emu68-tools.readme.in Emu68-tools.readme @ONLY)
install(FILES ${CMAKE_BINARY_DIR}/Emu68-tools.readme DESTINATION .)
