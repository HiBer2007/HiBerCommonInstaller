# hci_core - generic install core, zero Qt (C++17 + nlohmann-json + Lua + libzippp + cpr)
add_library(hci_core
    src/bus.cpp
    src/context.cpp
    src/elevation.cpp
    src/entry.cpp
    src/exec.cpp
    src/extension_registry.cpp
    src/lang.cpp
    src/log.cpp
    src/payload.cpp
    src/port.cpp
    src/product.cpp
    src/script_lua.cpp
    src/vars.cpp
)
if(HCI_CORE_SHARED)
    set_target_properties(hci_core PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
target_include_directories(hci_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    ${LUA_INCLUDE_DIR}
)
target_link_libraries(hci_core PUBLIC
    nlohmann_json::nlohmann_json
    ${LUA_LIBRARIES}
    ZLIB::ZLIB
    libzippp::libzippp
    cpr::cpr
)
if(WIN32)
    target_link_libraries(hci_core PRIVATE advapi32 shell32 ole32)
endif()