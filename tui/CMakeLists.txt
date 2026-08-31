# hci_tui_shell - C++ ANSI TUI shell (zero Qt, wcwidth-based layout)
add_executable(hci_tui
    src/main.cpp
    src/tui_shell.cpp
)
target_include_directories(hci_tui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../flow/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../ext/include
)
target_link_libraries(hci_tui PRIVATE
    hci_core
    hci_flow
    hci_ext
)
hci_add_version_info(hci_tui "HiBer Common Installer TUI" "HiBer Common Installer Module")