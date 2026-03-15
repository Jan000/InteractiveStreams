cmake_minimum_required(VERSION 3.20)

# CMake 3.31 rejects legacy policy baselines from older third-party CMakeLists.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# ─── Test Framework: doctest (header-only) ────────────────────────────────────
FetchContent_Declare(
    doctest
    URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.11.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(doctest)
set(CMAKE_WARN_DEPRECATED ON CACHE BOOL "" FORCE)

# ─── Test sources ─────────────────────────────────────────────────────────────
set(TEST_SOURCES
    test_main.cpp
    test_Config.cpp
    test_ChatMessage.cpp
    test_Player.cpp
    test_PhysicsWorld.cpp
    test_GameRegistry.cpp
    test_LocalPlatform.cpp
    test_Grid.cpp
    test_ColorConquest.cpp
    test_GravityBrawl.cpp
)

# ─── Tested source files (no main.cpp) ───────────────────────────────────────
set(TESTED_SOURCES
    ${CMAKE_SOURCE_DIR}/src/core/Config.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Logger.cpp
    ${CMAKE_SOURCE_DIR}/src/core/AudioManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/AudioMixer.cpp
    ${CMAKE_SOURCE_DIR}/src/core/AvatarCache.cpp
    ${CMAKE_SOURCE_DIR}/src/games/GameRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/games/chaos_arena/PhysicsWorld.cpp
    ${CMAKE_SOURCE_DIR}/src/games/gravity_brawl/GravityBrawl.cpp
    ${CMAKE_SOURCE_DIR}/src/platform/local/LocalPlatform.cpp
    ${CMAKE_SOURCE_DIR}/src/games/color_conquest/Grid.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/Background.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/PostProcessing.cpp
)

add_executable(InteractiveStreams_Tests
    ${TEST_SOURCES}
    ${TESTED_SOURCES}
)

target_include_directories(InteractiveStreams_Tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(InteractiveStreams_Tests PRIVATE
    doctest::doctest
    sfml-graphics
    sfml-window
    sfml-system
    sfml-network
    sfml-audio
    box2d
    nlohmann_json::nlohmann_json
    httplib::httplib
    spdlog::spdlog
)

if(WIN32)
    target_link_libraries(InteractiveStreams_Tests PRIVATE winhttp)
endif()

# Precompiled header (same as main project)
target_precompile_headers(InteractiveStreams_Tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/pch.h
)

# Copy DLLs for test executable (Windows)
if(WIN32)
    foreach(_target sfml-graphics sfml-window sfml-system sfml-network sfml-audio spdlog::spdlog box2d)
        add_custom_command(TARGET InteractiveStreams_Tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${_target}> $<TARGET_FILE_DIR:InteractiveStreams_Tests>
        )
    endforeach()
    # OpenAL DLL (required by SFML Audio)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_OPENAL_ARCH "x64")
    else()
        set(_OPENAL_ARCH "x86")
    endif()
    set(_OPENAL_DLL "${sfml_SOURCE_DIR}/extlibs/bin/${_OPENAL_ARCH}/openal32.dll")
    if(EXISTS "${_OPENAL_DLL}")
        add_custom_command(TARGET InteractiveStreams_Tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_OPENAL_DLL}" $<TARGET_FILE_DIR:InteractiveStreams_Tests>
        )
    endif()
endif()

# Register with CTest
list(APPEND CMAKE_MODULE_PATH ${doctest_SOURCE_DIR}/scripts/cmake)
include(doctest)
doctest_discover_tests(InteractiveStreams_Tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
