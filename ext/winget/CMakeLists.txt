# hci_winget - winget download/install backend extension (Windows).
add_library(hci_winget
    src/winget_ext.cpp
)
target_include_directories(hci_winget PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
)
target_link_libraries(hci_winget PUBLIC hci_core hci_ext)