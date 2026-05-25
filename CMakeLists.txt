cmake_minimum_required(VERSION 3.25)

project(ClipSoul VERSION 1.0.2 LANGUAGES CXX)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(clipsoul_core
    src/HistoryStore.cpp
    src/PasteModel.cpp
    src/PopupLayout.cpp
    src/StorageConfig.cpp
    src/TextUtil.cpp
)

target_include_directories(clipsoul_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(clipsoul_core PUBLIC
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

if(MSVC)
    target_compile_options(clipsoul_core PUBLIC /utf-8)
endif()

target_link_libraries(clipsoul_core PUBLIC
    winsqlite3
)

add_executable(clipsoul WIN32
    src/App.cpp
    src/ClipboardCapture.cpp
    src/ClipboardMonitor.cpp
    src/ClipSoulResources.rc
    src/ClipSoul.manifest
    src/PasteController.cpp
    src/PopupWindow.cpp
    src/SettingsWindow.cpp
    src/TrayIcon.cpp
    src/Win32Util.cpp
    src/main.cpp
)

target_link_libraries(clipsoul PRIVATE
    clipsoul_core
    user32
    shell32
    shlwapi
    d2d1
    dwrite
    dwmapi
    ole32
    uiautomationcore
    windowscodecs
    comctl32
    imm32
)

if(MSVC)
    target_compile_options(clipsoul PRIVATE /utf-8)
    target_link_options(clipsoul PRIVATE "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/src/ClipSoul.manifest")
endif()

enable_testing()

add_executable(clipsoul_tests
    tests/AppSettingsTests.cpp
    tests/HistoryStoreTests.cpp
    tests/PasteModelTests.cpp
    tests/PopupLayoutTests.cpp
    tests/TextUtilTests.cpp
    tests/test_main.cpp
)

target_link_libraries(clipsoul_tests PRIVATE
    clipsoul_core
)

if(MSVC)
    target_compile_options(clipsoul_tests PRIVATE /utf-8)
endif()

add_test(NAME clipsoul_tests COMMAND clipsoul_tests)
