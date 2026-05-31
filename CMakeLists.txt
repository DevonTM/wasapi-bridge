cmake_minimum_required(VERSION 3.10)
project(wasapi-bridge C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/external/miniaudio)

set(PROJECT_SOURCES
    src/main.cpp
    src/callbacks.cpp
    src/device_manager.cpp
    src/gui/about_tab.cpp
    src/gui/bridge_service.cpp
    src/gui/bridge_tab.cpp
    src/gui/device_enum.cpp
    src/gui/log_tab.cpp
    src/gui/logger.cpp
    src/gui/main_window.cpp
    src/gui/tray.cpp
    src/gui/ui_utils.cpp
)

set(THIRD_PARTY_SOURCES
    src/miniaudio_impl.c
)

set(RESOURCES
    resources/wasapi-bridge.rc
)

add_executable(${PROJECT_NAME} WIN32
    ${PROJECT_SOURCES}
    ${THIRD_PARTY_SOURCES}
    ${RESOURCES}
)

set_source_files_properties(resources/wasapi-bridge.rc PROPERTIES
    LANGUAGE RC
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(MSVC)
    set_source_files_properties(${PROJECT_SOURCES} PROPERTIES
        COMPILE_OPTIONS "/W4")
else()
    set_source_files_properties(${PROJECT_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic;-Wshadow")
endif()

add_definitions(-DWINVER=0x0601 -D_WIN32_WINNT=0x0601 -DUNICODE -D_UNICODE -DNOMINMAX)

set_target_properties(${PROJECT_NAME} PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
)
if(MINGW)
    target_link_options(${PROJECT_NAME} PRIVATE
        -municode
        -Wl,--build-id
        $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:-static>
        $<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:-s>
    )
elseif(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:/MT>
        $<$<CONFIG:Debug>:/MTd>
    )
    target_link_options(${PROJECT_NAME} PRIVATE
        /MANIFEST:NO
        $<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:/OPT:REF>
        $<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:/OPT:ICF>
    )
endif()

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ole32 uuid
        comctl32
        gdi32 user32
        shell32 comdlg32
        uxtheme
    )
endif()
