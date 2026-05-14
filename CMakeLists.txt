cmake_minimum_required(VERSION 3.20)
project(AileEx LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static CRT linking: single EXE distribution (no dependency on msvcp140.dll / vcruntime140.dll)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(AileEx WIN32
    src/main.cpp
    src/App.cpp
    src/MainWindow.cpp
    src/CompressDlg.cpp
    src/CompressHelper.cpp
    src/AdvancedCompressDlg.cpp
    src/RarAdvancedDlg.cpp
    src/InfoDlg.cpp
    src/PropertiesDlg.cpp
    src/CommentDlg.cpp
    src/ProgressDlg.cpp
    src/SettingsDlg.cpp
    src/Settings.cpp
    src/I18n.cpp
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
    version
)

if(MSVC)
    target_compile_options(AileEx PRIVATE /W3 /utf-8)

    # Note: CMake 3.15+ with Ninja+MSVC automatically adds /showIncludes.
    # To prevent verbose include file output, run without --verbose flag:
    #   cmake --build build_release       (no output)
    #   cmake --build build_release -v    (shows include paths)

    # Disable auto-manifest; the manifest is embedded via AileEx.rc (1 RT_MANIFEST)
    target_link_options(AileEx PRIVATE "/MANIFEST:NO")

    # Release build optimizations:
    #   /GR-      Disable RTTI (dynamic_cast/typeid unused, reduces binary size)
    #   /guard:cf Control Flow Guard (mitigates ROP attacks)
    #   /Zi       Generate PDB (retained in Release for crash analysis)
    target_compile_options(AileEx PRIVATE
        $<$<CONFIG:Release>:/GR->
        $<$<CONFIG:Release>:/guard:cf>
        $<$<CONFIG:Release>:/Zi>
    )
    target_link_options(AileEx PRIVATE
        $<$<CONFIG:Release>:/guard:cf>
        $<$<CONFIG:Release>:/DEBUG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
    )
endif()

# Link-Time Optimization (LTO) — Release only. Enables cross-translation-unit inlining.
include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_OK OUTPUT IPO_ERR)
if(IPO_OK)
    set_property(TARGET AileEx PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
else()
    message(STATUS "IPO/LTO not supported: ${IPO_ERR}")
endif()
