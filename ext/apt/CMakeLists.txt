# hci_apt - apt download/install backend extension (POSIX/Linux).
add_library(hci_apt
    src/apt_ext.cpp
)
target_include_directories(hci_apt PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
)
target_link_libraries(hci_apt PUBLIC hci_core hci_ext)