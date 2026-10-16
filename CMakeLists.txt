cmake_minimum_required(VERSION 3.21)
project(FlyGlow VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent LinguistTools)
qt_standard_project_setup()
set(CMAKE_AUTORCC ON)

if(MSVC)
    add_compile_options(/utf-8 /W3 /permissive-)
endif()

file(GLOB FG_CORE_SOURCES CONFIGURE_DEPENDS
    src/core/*.cpp src/core/*.h
    src/core/effects/*.cpp src/core/effects/*.h)
add_library(flyglow_core STATIC ${FG_CORE_SOURCES})
target_include_directories(flyglow_core PUBLIC src)
target_link_libraries(flyglow_core PUBLIC Qt6::Core Qt6::Gui)

file(GLOB FG_DEVICE_SOURCES CONFIGURE_DEPENDS src/device/*.cpp src/device/*.h)
file(GLOB FG_MEDIA_SOURCES CONFIGURE_DEPENDS src/media/*.cpp src/media/*.h)
file(GLOB FG_UI_SOURCES CONFIGURE_DEPENDS src/ui/*.cpp src/ui/*.h)
file(GLOB FG_SHADER_SOURCES CONFIGURE_DEPENDS src/shader/*.cpp src/shader/*.h)

qt_add_executable(FlyGlow WIN32 src/main.cpp ${FG_UI_SOURCES} ${FG_DEVICE_SOURCES} ${FG_MEDIA_SOURCES}
    ${FG_SHADER_SOURCES})
target_link_libraries(FlyGlow PRIVATE flyglow_core Qt6::Widgets Qt6::Network Qt6::Concurrent)
target_compile_definitions(FlyGlow PRIVATE FLYGLOW_VERSION="${PROJECT_VERSION}"
    FLYGLOW_VERSION_RC=${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0)
if(EXISTS ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
    target_sources(FlyGlow PRIVATE resources/resources.qrc)
endif()
if(WIN32)
    target_link_libraries(FlyGlow PRIVATE hid setupapi mfplat mfreadwrite mfuuid ole32)
    if(EXISTS ${CMAKE_SOURCE_DIR}/resources/app.rc)
        target_sources(FlyGlow PRIVATE resources/app.rc)
    endif()
endif()

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT FlyGlow)
set_target_properties(FlyGlow PROPERTIES VS_DEBUGGER_ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>;%PATH%")

file(GLOB FG_TS_FILES CONFIGURE_DEPENDS translations/*.ts)
if(FG_TS_FILES)
    qt_add_lupdate(TS_FILES ${FG_TS_FILES} SOURCE_TARGETS FlyGlow flyglow_core
                   LUPDATE_TARGET flyglow_lupdate)
    qt_add_lrelease(TS_FILES ${FG_TS_FILES} QM_FILES_OUTPUT_VARIABLE FG_QM_FILES
                    LRELEASE_TARGET flyglow_lrelease)
    qt_add_resources(FlyGlow flyglow_i18n PREFIX /i18n BASE ${CMAKE_CURRENT_BINARY_DIR}
                     FILES ${FG_QM_FILES})
endif()

if(EXISTS ${CMAKE_SOURCE_DIR}/tools/media_check.cpp)
    qt_add_executable(media_check tools/media_check.cpp ${FG_MEDIA_SOURCES})
    target_link_libraries(media_check PRIVATE flyglow_core Qt6::Widgets Qt6::Concurrent)
    if(WIN32)
        target_link_libraries(media_check PRIVATE mfplat mfreadwrite mfuuid ole32)
    endif()
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/tools/effects_check.cpp)
    qt_add_executable(effects_check tools/effects_check.cpp)
    target_link_libraries(effects_check PRIVATE flyglow_core)
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/tools/shader_check.cpp)
    qt_add_executable(shader_check tools/shader_check.cpp ${FG_SHADER_SOURCES}
        src/media/LedSampler.cpp src/media/LedSampler.h)
    target_link_libraries(shader_check PRIVATE flyglow_core Qt6::Concurrent)
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/tools/ipc_probe.cpp)
    qt_add_executable(ipc_probe tools/ipc_probe.cpp ${FG_DEVICE_SOURCES})
    target_link_libraries(ipc_probe PRIVATE flyglow_core Qt6::Network Qt6::Concurrent)
    if(WIN32)
        target_link_libraries(ipc_probe PRIVATE hid setupapi)
    endif()
endif()
