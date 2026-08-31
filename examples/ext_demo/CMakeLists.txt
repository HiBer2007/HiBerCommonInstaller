# hci_ext_demo - example extension (drop-in DLL, also packageable as .hci)
add_library(hci_ext_demo SHARED
    src/ext_demo.cpp
)
target_include_directories(hci_ext_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../ext/include
)
target_link_libraries(hci_ext_demo PRIVATE hci_core hci_ext)
set_target_properties(hci_ext_demo PROPERTIES PREFIX "")

# Package demo: meta.json + dll -> demo.hci (ZIP container).
set(HCI_PKG_STAGE "${CMAKE_CURRENT_BINARY_DIR}/pkg")
add_custom_target(hci_ext_demo_pkg
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${HCI_PKG_STAGE}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${HCI_PKG_STAGE}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:hci_ext_demo> ${HCI_PKG_STAGE}/
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File
            ${CMAKE_CURRENT_SOURCE_DIR}/make_meta.ps1
            ${HCI_PKG_STAGE} $<TARGET_FILE:hci_ext_demo>
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
            "Compress-Archive -Path '${HCI_PKG_STAGE}/*' -DestinationPath '$<TARGET_FILE_DIR:hci_ext_demo>/demo.hci' -Force"
    DEPENDS hci_ext_demo
    COMMENT "Packaging hci_ext_demo.hci (ZIP: meta.json + dll)"
)