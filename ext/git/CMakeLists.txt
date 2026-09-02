# hci_git - generic Git strategy extension (reusable by any product needing
# Git during install: system vs bundled decision, Git for Windows download).
add_library(hci_git
    src/git_ext.cpp
)
target_include_directories(hci_git PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
)
target_link_libraries(hci_git PUBLIC hci_core hci_ext)