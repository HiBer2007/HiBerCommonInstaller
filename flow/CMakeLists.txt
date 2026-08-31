# hci_flow - flow controller (JSON declarative steps + Lua script blocks)
add_library(hci_flow
    src/flow.cpp
)
target_include_directories(hci_flow PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(hci_flow PUBLIC hci_core)