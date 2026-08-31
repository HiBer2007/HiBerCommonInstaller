# Demo product: no compiled targets; files are copied to the build dir and
# hci_demo_run executes the install flow end-to-end (silent, --path).
file(COPY demo DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# Extension demo (drop-in DLL + .hci package modes).
add_subdirectory(ext_demo)

add_custom_target(hci_demo_run
    COMMAND hci_cli --product ${CMAKE_CURRENT_BINARY_DIR}/demo/product.json
                    --flow install --silent
                    --path ${CMAKE_CURRENT_BINARY_DIR}/demo_out
                    --extensions ${CMAKE_CURRENT_BINARY_DIR}/demo/extensions
    DEPENDS hci_cli
    COMMENT "Running hci demo install flow (silent, exit code = status)"
)

add_custom_target(hci_demo_uninstall
    COMMAND hci_cli --product ${CMAKE_CURRENT_BINARY_DIR}/demo/product.json
                    --flow uninstall --silent
                    --path ${CMAKE_CURRENT_BINARY_DIR}/demo_out
    DEPENDS hci_cli
    COMMENT "Running hci demo uninstall flow (silent)"
)