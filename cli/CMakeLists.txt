# hci_cli - CLI shell (console subsystem, zero Qt)
add_executable(hci_cli
    src/main.cpp
)
target_include_directories(hci_cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../flow/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../ext/include
)
target_link_libraries(hci_cli PRIVATE hci_core hci_flow hci_ext)
target_compile_definitions(hci_cli PRIVATE HCI_VERSION_STRING="${HCI_VERSION}")
hci_add_version_info(hci_cli "HiBer Common Installer CLI" "HiBer Common Installer Module")