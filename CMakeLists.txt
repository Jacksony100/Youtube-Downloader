cmake_minimum_required(VERSION 3.21)

project(VideoDownloaderPro VERSION 4.0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Widgets Network Test)
qt_standard_project_setup()

qt_add_executable(VideoDownloaderPro WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/core.cpp
    src/core.hpp
    src/main_window.cpp
    src/main_window.hpp
)

qt_add_resources(VideoDownloaderPro app_resources
    PREFIX "/"
    FILES
        ui/styles/dark.qss
        icon.ico
)

target_link_libraries(VideoDownloaderPro PRIVATE Qt6::Core Qt6::Widgets Qt6::Network)
target_include_directories(VideoDownloaderPro PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(VideoDownloaderPro PROPERTIES QT_NO_ENTRYPOINT ON)
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
    qt_add_executable(VideoDownloaderProTests tests_cpp/test_core.cpp src/core.cpp src/core.hpp)
    target_link_libraries(VideoDownloaderProTests PRIVATE Qt6::Core Qt6::Network Qt6::Test)
    target_include_directories(VideoDownloaderProTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(VideoDownloaderProTests PRIVATE VDP_VERSION="${PROJECT_VERSION}")
    add_test(NAME core-tests COMMAND VideoDownloaderProTests)
endif()

install(TARGETS VideoDownloaderPro
    BUNDLE DESTINATION .
    RUNTIME DESTINATION .
)
