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
    src/qrc_source.cpp
    src/resource_utils.cpp
)

qt_add_executable(hci_gui ${GUI_SOURCES})
# Console subsystem on purpose (entry model: keep console when launched from
# a terminal; release it on double-click).
set_target_properties(hci_gui PROPERTIES AUTOMOC ON)

# 产物命名：默认 HCInstaller_Template_<HCI_VERSION>.exe；宿主可用
# HCI_GUI_OUTPUT_NAME 覆盖（如 NSUM 特化 = NSUM_Installer_<版本>.exe）。
if(NOT DEFINED HCI_GUI_OUTPUT_NAME)
    set(HCI_GUI_OUTPUT_NAME "HCInstaller_Template_${HCI_VERSION}")
endif()
set_target_properties(hci_gui PROPERTIES OUTPUT_NAME "${HCI_GUI_OUTPUT_NAME}")
target_compile_definitions(hci_gui PRIVATE HCI_VERSION_STRING="${HCI_VERSION}")

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

# Optional embedded product files (single-file distribution):
# HCI_PRODUCT_FILES supports "alias=path" entries; alias defaults to basename.
# Example: product.json=<file>;deploy/...=<file>;...
if(HCI_PRODUCT_FILES)
    # The .qrc filename carries a content hash of ALL embedded files so that
    # editing an embedded file (product.json/flow/config) forces a NEW rcc
    # target - AUTORCC alone only re-runs when the .qrc itself changes.
    set(_prod_hash "")
    foreach(_f ${HCI_PRODUCT_FILES})
        set(_path "${_f}")
        if(_path MATCHES "^(.*)=(.*)$")
            set(_path "${CMAKE_MATCH_2}")
        endif()
        if(EXISTS "${_path}")
            file(MD5 "${_path}" _md5)
            string(SUBSTRING "${_md5}" 0 8 _md5)
            string(APPEND _prod_hash "${_md5}")
        endif()
    endforeach()
    string(MD5 _prod_qrc_name "${_prod_hash}")
    set(_prod_qrc "${CMAKE_CURRENT_BINARY_DIR}/hci_product_${_prod_qrc_name}.qrc")
    file(WRITE "${_prod_qrc}" "<RCC>\n  <qresource prefix=\"/\">\n")
    foreach(_f ${HCI_PRODUCT_FILES})
        set(_alias "")
        if(_f MATCHES "^(.*)=(.*)$")
            set(_alias "${CMAKE_MATCH_1}")
            set(_f "${CMAKE_MATCH_2}")
        endif()
        if(_alias STREQUAL "")
            get_filename_component(_alias "${_f}" NAME)
        endif()
        file(APPEND "${_prod_qrc}" "    <file alias=\"${_alias}\">${_f}</file>\n")
    endforeach()
    file(APPEND "${_prod_qrc}" "  </qresource>\n</RCC>\n")
    target_sources(hci_gui PRIVATE "${_prod_qrc}")
    set_target_properties(hci_gui PROPERTIES AUTORCC ON)
    # Resource registration happens via Q_INIT_RESOURCE(hci_product) in main.cpp.
    target_compile_definitions(hci_gui PRIVATE HCI_EMBED_PRODUCT=1)
    message(STATUS "hci_gui: embedding product files (qrc hash ${_prod_qrc_name}): ${HCI_PRODUCT_FILES}")
endif()

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