# hci_ext - extension host (interfaces + three load modes + service registry)
add_library(hci_ext
    src/extension_loader.cpp
)
target_include_directories(hci_ext PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(hci_ext PUBLIC hci_core)

# Generic Git strategy extension (reusable by host products).
add_subdirectory(git)