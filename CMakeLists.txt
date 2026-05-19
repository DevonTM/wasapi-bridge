cmake_minimum_required(VERSION 3.10)
project(wasapi-bridge C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/external/miniaudio)

# Project sources get strict warnings; miniaudio_impl.c is a third-party
# amalgamation kept separate so its own diagnostics don't drown ours.
set(PROJECT_SOURCES
    src/main.cpp
    src/callbacks.cpp
    src/device_manager.cpp
    src/user_interface.cpp
)
set(THIRD_PARTY_SOURCES
    src/miniaudio_impl.c
)

add_executable(${PROJECT_NAME} ${PROJECT_SOURCES} ${THIRD_PARTY_SOURCES})

if(MSVC)
    set_source_files_properties(${PROJECT_SOURCES} PROPERTIES
        COMPILE_OPTIONS "/W4")
else()
    set_source_files_properties(${PROJECT_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic;-Wshadow")
endif()


# Windows 7 support target
add_definitions(-DWINVER=0x0601 -D_WIN32_WINNT=0x0601)

# Static linking and LTO optimizations for Release builds
set_target_properties(${PROJECT_NAME} PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
)
if(MINGW)
    target_link_options(${PROJECT_NAME} PRIVATE
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
        $<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:/OPT:REF>
        $<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:/OPT:ICF>
    )
endif()

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ole32 uuid)
endif()
