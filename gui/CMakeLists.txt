# hci_gui_shell - Qt GUI shell (Qt6 Widgets + HiBerGUILibrary, flow-driven pages)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)

# HiBerGUILibCPP submodule: skip when the host project already provides it.
if(NOT TARGET HiBerGUILibrary)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../HiBerGUILibCPP ${CMAKE_CURRENT_BINARY_DIR}/HiBerGUILibCPP)
endif()

set(GUI_SOURCES
    src/main.cpp
    src/gui_shell.cpp
    src/pages.cpp
)

qt_add_executable(hci_gui ${GUI_SOURCES})
# Console subsystem on purpose (entry model: keep console when launched from
# a terminal; release it on double-click).
set_target_properties(hci_gui PROPERTIES AUTOMOC ON)

target_include_directories(hci_gui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../flow/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../ext/include
)

target_link_libraries(hci_gui PRIVATE
    hci_core
    hci_flow
    hci_ext
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    HiBerGUILibrary
)

hci_add_version_info(hci_gui "HiBer Common Installer GUI" "HiBer Common Installer Module")

# Static Qt handling (HCI_QT_STATIC=ON or vcpkg static triplet).
if(HCI_QT_STATIC OR VCPKG_TARGET_TRIPLET MATCHES "static")
    if(MSVC)
        qt_import_plugins(hci_gui INCLUDE Qt::QWindowsIntegrationPlugin)
        message(STATUS "hci_gui: static Qt, QWindowsIntegrationPlugin imported")
    endif()
elseif(WIN32)
    get_target_property(_qt_qmake Qt6::qmake IMPORTED_LOCATION)
    if(_qt_qmake)
        get_filename_component(_qt_bin "${_qt_qmake}" DIRECTORY)
        find_program(_WD NAMES windeployqt PATHS "${_qt_bin}" NO_DEFAULT_PATH)
        if(_WD)
            add_custom_command(TARGET hci_gui POST_BUILD
                COMMAND "${_WD}" \"$<TARGET_FILE:hci_gui>\"
                    --no-translations --no-opengl-sw --no-compiler-runtime
                COMMENT "windeployqt hci_gui (dynamic)..."
            )
        endif()
    endif()
endif()