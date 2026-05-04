cmake_minimum_required(VERSION 3.20)
project(AileEx LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(AileEx WIN32
    src/main.cpp
    src/App.cpp
    src/MainWindow.cpp
    src/CompressDlg.cpp
    src/InfoDlg.cpp
    src/ProgressDlg.cpp
    src/SettingsDlg.cpp
    src/Settings.cpp
    src/SevenZip.cpp
    src/UnrarDll.cpp
    src/RarProcess.cpp
    src/WorkerThread.cpp
    res/AileEx.rc
)

target_include_directories(AileEx PRIVATE
    src
    sdk
)

target_compile_definitions(AileEx PRIVATE
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(AileEx PRIVATE
    comctl32
    shlwapi
    shell32
    ole32
    oleaut32
    advapi32
    comdlg32
)

if(MSVC)
    target_compile_options(AileEx PRIVATE /W3 /utf-8)
    # Disable auto-manifest; the manifest is embedded via AileEx.rc (1 RT_MANIFEST)
    target_link_options(AileEx PRIVATE "/MANIFEST:NO")
endif()
