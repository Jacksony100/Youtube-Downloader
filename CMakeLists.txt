cmake_minimum_required(VERSION 3.21)

project(VideoDownloaderPro VERSION 5.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Widgets Network Test)
qt_standard_project_setup()

file(WRITE "${CMAKE_BINARY_DIR}/version.txt" "${PROJECT_VERSION}\n")
file(GLOB_RECURSE VDP_SERVICE_SOURCES CONFIGURE_DEPENDS
    src/downloads/*.cpp src/downloads/*.hpp
    src/metadata/*.cpp src/metadata/*.hpp
    src/runtime/*.cpp src/runtime/*.hpp
    src/storage/*.cpp src/storage/*.hpp
    src/diagnostics/*.cpp src/diagnostics/*.hpp)
add_library(vdp_core STATIC src/core.cpp src/core.hpp ${VDP_SERVICE_SOURCES})
target_link_libraries(vdp_core PUBLIC Qt6::Core Qt6::Network)
target_include_directories(vdp_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(vdp_core PUBLIC VDP_VERSION="${PROJECT_VERSION}")
if(MSVC)
    add_compile_options(/utf-8)
    target_compile_options(vdp_core PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(vdp_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

qt_add_executable(VideoDownloaderPro WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/main_window.cpp
    src/main_window.hpp
)

qt_add_resources(VideoDownloaderPro app_resources
    PREFIX "/"
    FILES
        ui/styles/dark.qss
        icon.ico
        runtime/toolchain-lock.json
)

target_link_libraries(VideoDownloaderPro PRIVATE vdp_core Qt6::Widgets)
target_include_directories(VideoDownloaderPro PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(VideoDownloaderPro PROPERTIES QT_NO_ENTRYPOINT ON)
set_target_properties(VideoDownloaderPro PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
target_compile_definitions(VideoDownloaderPro PRIVATE
    VDP_VERSION="${PROJECT_VERSION}"
    VDP_REPOSITORY="https://github.com/Jacksony100/Youtube-Downloader"
)

if(MSVC)
    target_compile_options(VideoDownloaderPro PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(VideoDownloaderPro PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    qt_add_executable(engine_smoke_arguments scripts/engine_smoke_arguments.cpp)
    target_link_libraries(engine_smoke_arguments PRIVATE vdp_core)
    set_target_properties(engine_smoke_arguments PROPERTIES QT_NO_ENTRYPOINT ON)
    find_package(Python3 3.11 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME release-script-security-tests COMMAND ${Python3_EXECUTABLE} -m unittest discover
            -s ${CMAKE_CURRENT_SOURCE_DIR}/scripts -p test_release_tools.py -v)
        set_tests_properties(release-script-security-tests PROPERTIES TIMEOUT 60)
    endif()
    file(GLOB VDP_TEST_SOURCES CONFIGURE_DEPENDS tests_cpp/test_*.cpp)
    foreach(test_source IN LISTS VDP_TEST_SOURCES)
        get_filename_component(test_name "${test_source}" NAME_WE)
        qt_add_executable(${test_name} "${test_source}")
        target_link_libraries(${test_name} PRIVATE vdp_core Qt6::Test)
        if(test_name STREQUAL "test_main_window")
            target_sources(${test_name} PRIVATE src/main_window.cpp src/main_window.hpp)
            target_link_libraries(${test_name} PRIVATE Qt6::Widgets)
            target_compile_definitions(${test_name} PRIVATE VDP_REPOSITORY="https://github.com/Jacksony100/Youtube-Downloader")
            qt_add_resources(${test_name} ui_resources PREFIX "/" FILES ui/styles/dark.qss icon.ico)
        endif()
        qt_add_resources(${test_name} runtime_resources PREFIX "/" FILES runtime/toolchain-lock.json)
        add_test(NAME ${test_name} COMMAND ${test_name})
        set_tests_properties(${test_name} PROPERTIES TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()
endif()

install(TARGETS VideoDownloaderPro
    BUNDLE DESTINATION .
    RUNTIME DESTINATION .
)
