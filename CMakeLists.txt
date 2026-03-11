cmake_minimum_required(VERSION 3.20)
project(InteractiveStreams VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(GIT_COMMIT_HASH_OVERRIDE "" CACHE STRING "Override git commit hash for builds without .git metadata")

# ─── Git commit hash for version info ────────────────────────────────────────
if(GIT_COMMIT_HASH_OVERRIDE)
    set(GIT_COMMIT_HASH "${GIT_COMMIT_HASH_OVERRIDE}")
else()
    execute_process(
        COMMAND git rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
if(NOT GIT_COMMIT_HASH)
    set(GIT_COMMIT_HASH "unknown")
endif()
message(STATUS "Git commit hash: ${GIT_COMMIT_HASH}")
add_compile_definitions(IS_GIT_HASH="${GIT_COMMIT_HASH}")

# ─── Speed: FetchContent – kein Git-Update-Check für bereits gecachte Deps ───
# Verhindert langsame Netzwerk-Abfragen bei jedem cmake --preset.
# Neu klonen: 'build/_deps/*-subbuild' löschen und cmake --preset erneut aufrufen.
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

# ─── Speed: MSVC – parallele Kompilierung (/MP) + Warnings aus Deps ──────────
# /FS ist bei /MP zwingend erforderlich: verhindert C1041-Fehler wenn mehrere
# cl.exe-Prozesse gleichzeitig in dieselbe .pdb-Datei schreiben wollen.
if(MSVC)
    add_compile_options(/MP /FS)
    add_compile_options(/wd4996)
endif()

# Options
option(IS_BUILD_TESTS "Build tests" OFF)
option(IS_USE_SYSTEM_LIBS "Use system-installed libraries instead of FetchContent" OFF)
option(IS_USE_YOUTUBE_GRPC
    "Enable YouTube gRPC streaming chat API. Replaces REST polling with a \
long-lived gRPC stream for near-instant message delivery. First build pulls \
gRPC + Protobuf + Abseil (~15 min compile on first run, cached afterwards)."
    ON)

# ─── Dependencies via FetchContent ────────────────────────────────────────────
include(FetchContent)

# SFML 2.6 – Rendering & Window
FetchContent_Declare(
    sfml
    URL https://github.com/SFML/SFML/archive/refs/tags/2.6.1.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# Box2D – Physics
FetchContent_Declare(
    box2d
    URL https://github.com/erincatto/box2d/archive/refs/tags/v2.4.1.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(BOX2D_BUILD_TESTBED OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)

# nlohmann/json – JSON handling
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# cpp-httplib – Embedded HTTP server for admin dashboard
FetchContent_Declare(
    httplib
    URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.15.3.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# spdlog – Logging
FetchContent_Declare(
    spdlog
    URL https://github.com/gabime/spdlog/archive/refs/tags/v1.13.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# stb – Header-only image utilities (JPEG encoding for web preview)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)

# SQLite3 – amalgamation via sqlite_modern_cpp wrapper alternative:
# We use the raw amalgamation from the official mirror for maximum compatibility.
FetchContent_Declare(
    sqlite3
    URL https://www.sqlite.org/2024/sqlite-amalgamation-3450300.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# Suppress deprecation warnings from older CMakeLists in dependencies
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(sfml box2d json httplib spdlog)
set(CMAKE_WARN_DEPRECATED ON CACHE BOOL "" FORCE)

# stb has no CMakeLists.txt – populate only (no add_subdirectory)
# CMP0169 was introduced in CMake 3.30; guard to support older versions (e.g. Ubuntu 24.04 ships 3.28)
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()
FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
    FetchContent_Populate(stb)
endif()

# SQLite3 amalgamation – no CMakeLists.txt, create a static library
FetchContent_GetProperties(sqlite3)
if(NOT sqlite3_POPULATED)
    FetchContent_Populate(sqlite3)
endif()
add_library(sqlite3_lib STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
target_include_directories(sqlite3_lib PUBLIC ${sqlite3_SOURCE_DIR})
if(MSVC)
    target_compile_options(sqlite3_lib PRIVATE /wd4706 /wd4100)
endif()

# ─── Optional: gRPC for YouTube streaming chat ───────────────────────────────
# When enabled, the YouTube platform uses a long-lived gRPC stream to receive
# chat messages in near-real-time instead of REST polling.  This pulls gRPC,
# Protobuf, Abseil, c-ares, re2 via FetchContent – the first build will take
# considerably longer (~15 min) but subsequent builds are cached.
if(IS_USE_YOUTUBE_GRPC)
    message(STATUS "YouTube gRPC streaming chat ENABLED – pulling gRPC dependencies …")

    # Disable parts of gRPC we don't need to speed up the build
    set(gRPC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(gRPC_BUILD_CSHARP_EXT OFF CACHE BOOL "" FORCE)
    set(gRPC_BUILD_GRPC_CSHARP_PLUGIN OFF CACHE BOOL "" FORCE)
    set(gRPC_BUILD_GRPC_NODE_PLUGIN OFF CACHE BOOL "" FORCE)
    set(gRPC_BUILD_GRPC_OBJECTIVE_C_PLUGIN OFF CACHE BOOL "" FORCE)
    set(gRPC_BUILD_GRPC_PHP_PLUGIN OFF CACHE BOOL "" FORCE)
    set(gRPC_BUILD_GRPC_PYTHON_PLUGIN OFF CACHE BOOL "" FORCE)
    set(gRPC_BUILD_GRPC_RUBY_PLUGIN OFF CACHE BOOL "" FORCE)
    set(gRPC_INSTALL OFF CACHE BOOL "" FORCE)
    set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)

    # Disable install targets for sub-projects to avoid CMake export-set
    # conflicts (protobuf install targets reference abseil targets that are
    # not exported when built as a sub-project via FetchContent).
    set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
    set(ABSL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(CARES_INSTALL OFF CACHE BOOL "" FORCE)
    set(RE2_BUILD_TESTING OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        gRPC
        GIT_REPOSITORY https://github.com/grpc/grpc.git
        GIT_TAG        v1.62.0
        GIT_SHALLOW    TRUE
    )
    set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(gRPC)
    set(CMAKE_WARN_DEPRECATED ON CACHE BOOL "" FORCE)

    # ── Proto generation ──────────────────────────────────────────────────
    set(_PROTO_DIR  "${CMAKE_SOURCE_DIR}/src/platform/youtube")
    set(_PROTO_GEN  "${CMAKE_CURRENT_BINARY_DIR}/proto-gen")
    file(MAKE_DIRECTORY "${_PROTO_GEN}")

    add_custom_command(
        OUTPUT
            "${_PROTO_GEN}/youtube_chat.pb.cc"
            "${_PROTO_GEN}/youtube_chat.pb.h"
            "${_PROTO_GEN}/youtube_chat.grpc.pb.cc"
            "${_PROTO_GEN}/youtube_chat.grpc.pb.h"
        COMMAND $<TARGET_FILE:protoc>
            --grpc_out "${_PROTO_GEN}"
            --cpp_out  "${_PROTO_GEN}"
            -I "${_PROTO_DIR}"
            --plugin=protoc-gen-grpc=$<TARGET_FILE:grpc_cpp_plugin>
            "${_PROTO_DIR}/youtube_chat.proto"
        DEPENDS "${_PROTO_DIR}/youtube_chat.proto" protoc grpc_cpp_plugin
        COMMENT "Generating C++ from youtube_chat.proto …"
    )

    set(YOUTUBE_GRPC_SOURCES
        "${_PROTO_GEN}/youtube_chat.pb.cc"
        "${_PROTO_GEN}/youtube_chat.grpc.pb.cc"
        src/platform/youtube/YouTubeGrpcChat.cpp
    )
else()
    message(STATUS "YouTube gRPC streaming chat DISABLED (using REST polling fallback)")
    set(YOUTUBE_GRPC_SOURCES "")
endif()

# ─── Source files ─────────────────────────────────────────────────────────────
set(CORE_SOURCES
    src/crash_handler.cpp
    src/core/Application.cpp
    src/core/Config.cpp
    src/core/GameManager.cpp
    src/core/Logger.cpp
    src/core/ChannelManager.cpp
    src/core/StreamInstance.cpp
    src/core/StreamManager.cpp
    src/core/StreamProfile.cpp
    src/core/PlayerDatabase.cpp
    src/core/PerfMonitor.cpp
    src/core/SettingsDatabase.cpp
    src/core/AudioManager.cpp
    src/core/AudioMixer.cpp
)

set(GAME_SOURCES
    src/games/GameRegistry.cpp
    src/games/chaos_arena/ChaosArena.cpp
    src/games/chaos_arena/Player.cpp
    src/games/chaos_arena/Arena.cpp
    src/games/chaos_arena/PhysicsWorld.cpp
    src/games/chaos_arena/ParticleSystem.cpp
    src/games/chaos_arena/Projectile.cpp
    src/games/chaos_arena/PowerUp.cpp
    src/games/color_conquest/ColorConquest.cpp
    src/games/color_conquest/Grid.cpp
    src/games/gravity_brawl/GravityBrawl.cpp
)

set(PLATFORM_SOURCES
    src/platform/local/LocalPlatform.cpp
    src/platform/twitch/TwitchPlatform.cpp
    src/platform/twitch/TwitchApi.cpp
    src/platform/youtube/YoutubePlatform.cpp
    src/platform/youtube/YouTubeApi.cpp
    src/platform/youtube/YouTubeQuota.cpp
)

set(RENDERING_SOURCES
    src/rendering/Renderer.cpp
    src/rendering/Background.cpp
    src/rendering/PostProcessing.cpp
    src/rendering/SpriteAnimator.cpp
)

set(STREAMING_SOURCES
    src/streaming/StreamEncoder.cpp
    src/streaming/stb_impl.cpp
)

set(WEB_SOURCES
    src/web/WebServer.cpp
    src/web/ApiRoutes.cpp
)

# ─── Main executable ─────────────────────────────────────────────────────────
add_executable(${PROJECT_NAME}
    src/main.cpp
    ${CORE_SOURCES}
    ${GAME_SOURCES}
    ${PLATFORM_SOURCES}
    ${RENDERING_SOURCES}
    ${STREAMING_SOURCES}
    ${WEB_SOURCES}
    ${YOUTUBE_GRPC_SOURCES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${stb_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    sfml-graphics
    sfml-window
    sfml-system
    sfml-network
    sfml-audio
    box2d
    nlohmann_json::nlohmann_json
    httplib::httplib
    spdlog::spdlog
    sqlite3_lib
)

# ─── YouTube gRPC: extra includes, libs, defines ─────────────────────────────
if(IS_USE_YOUTUBE_GRPC)
    target_include_directories(${PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/proto-gen"
    )
    target_link_libraries(${PROJECT_NAME} PRIVATE
        grpc++
        libprotobuf
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE IS_YOUTUBE_GRPC_ENABLED)
endif()

# ─── Speed: Precompiled Header ────────────────────────────────────────────────
# Alle schweren Headers (SFML, Box2D, nlohmann/json, spdlog, STL) werden einmal
# vorkompiliert. Das spart bei inkrementellen Builds mehrere Sekunden pro TU.
target_precompile_headers(${PROJECT_NAME} PRIVATE src/pch.h)

# ─── Linux: allow duplicate stb symbols ──────────────────────────────────────
# When SFML is built as a static library, its ImageLoader.cpp bundles stb_image
# and stb_image_write internally. Our stb_impl.cpp also defines them (needed for
# JPEG frame capture). GCC's linker treats this as an error; --allow-multiple-definition
# tells it to silently pick the first definition. Both copies are identical
# (same stb version), so the result is correct on all platforms.
if(UNIX AND NOT APPLE)
    target_link_options(${PROJECT_NAME} PRIVATE
        -Wl,--allow-multiple-definition
        -rdynamic  # export symbol table for backtrace_symbols() in crash_handler.cpp
    )
endif()

set(IS_COPY_IF_STALE_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/CopyIfStale.cmake")

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
        -DINPUT_DIR="${CMAKE_SOURCE_DIR}/assets"
        -DOUTPUT_DIR="$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets"
        -DSTAMP_FILE="${CMAKE_BINARY_DIR}/${PROJECT_NAME}_assets_$<CONFIG>.stamp"
        -P "${IS_COPY_IF_STALE_SCRIPT}"
    VERBATIM
)

# ─── Web Dashboard (Next.js static export) ───────────────────────────────────
# Incrementally rebuild the dashboard whenever any source file in web/src/
# changes.  Uses a stamp file so the build is skipped when nothing changed.
#
# New files added to web/src/ are picked up after the next `cmake --preset`.
# The bun executable must be in PATH (install from https://bun.sh).
find_program(BUN_EXECUTABLE NAMES bun bun.exe DOC "Bun package manager / bundler")

if(BUN_EXECUTABLE)
    # Collect web source files at configure time for dependency tracking.
    # NOTE: add new top-level config files here if needed.
    file(GLOB_RECURSE WEB_SOURCE_FILES
        "${CMAKE_SOURCE_DIR}/web/src/*.tsx"
        "${CMAKE_SOURCE_DIR}/web/src/*.ts"
        "${CMAKE_SOURCE_DIR}/web/src/*.css"
        "${CMAKE_SOURCE_DIR}/web/public/*"
    )
    list(APPEND WEB_SOURCE_FILES
        "${CMAKE_SOURCE_DIR}/web/package.json"
        "${CMAKE_SOURCE_DIR}/web/next.config.ts"
        "${CMAKE_SOURCE_DIR}/web/tsconfig.json"
        "${CMAKE_SOURCE_DIR}/web/postcss.config.mjs"
    )

    set(WEB_BUILD_STAMP "${CMAKE_BINARY_DIR}/web_dashboard.stamp")

    # Step 1: custom command – runs bun run build only when sources change.
    add_custom_command(
        OUTPUT  "${WEB_BUILD_STAMP}"
        COMMAND "${BUN_EXECUTABLE}" run build
        COMMAND "${CMAKE_COMMAND}" -E touch "${WEB_BUILD_STAMP}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/web"
        DEPENDS ${WEB_SOURCE_FILES}
        COMMENT "Building web dashboard (bun run build)..."
        VERBATIM
    )

    # Step 2: dashboard build target. Only the app depends on it, so test-only
    # builds do not pay the dashboard build cost.
    add_custom_target(WebDashboard DEPENDS "${WEB_BUILD_STAMP}")

    # Step 3: make the main executable wait for the dashboard before copying.
    add_dependencies(${PROJECT_NAME} WebDashboard)

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            -DINPUT_DIR="${CMAKE_SOURCE_DIR}/web/out"
            -DOUTPUT_DIR="$<TARGET_FILE_DIR:${PROJECT_NAME}>/dashboard"
            -DSTAMP_FILE="${CMAKE_BINARY_DIR}/web_dashboard_copy_$<CONFIG>.stamp"
            -DINPUT_STAMP="${WEB_BUILD_STAMP}"
            -P "${IS_COPY_IF_STALE_SCRIPT}"
        VERBATIM
    )
else()
    message(WARNING
        "bun not found in PATH – web dashboard will NOT be rebuilt automatically.\n"
        "Install bun (https://bun.sh) and re-run cmake --preset msvc-x64.")

    # Fallback: copy whatever is already in web/out/ if it exists.
    if(EXISTS "${CMAKE_SOURCE_DIR}/web/out")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                -DINPUT_DIR="${CMAKE_SOURCE_DIR}/web/out"
                -DOUTPUT_DIR="$<TARGET_FILE_DIR:${PROJECT_NAME}>/dashboard"
                -DSTAMP_FILE="${CMAKE_BINARY_DIR}/web_dashboard_copy_$<CONFIG>.stamp"
                -P "${IS_COPY_IF_STALE_SCRIPT}"
            VERBATIM
        )
    endif()
endif()

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
        -DINPUT_DIR="${CMAKE_SOURCE_DIR}/config"
        -DOUTPUT_DIR="$<TARGET_FILE_DIR:${PROJECT_NAME}>/config"
        -DSTAMP_FILE="${CMAKE_BINARY_DIR}/${PROJECT_NAME}_config_$<CONFIG>.stamp"
        -P "${IS_COPY_IF_STALE_SCRIPT}"
    VERBATIM
)

# Copy SFML DLLs next to executable (Windows)
if(WIN32)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:sfml-graphics> $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:sfml-window> $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:sfml-system> $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:sfml-network> $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
    # SFML Audio DLL
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:sfml-audio> $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
    # spdlog DLL
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:spdlog::spdlog> $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
    # Box2D DLL
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:box2d> $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
    # OpenAL DLL (required by SFML Audio) – auto-detect target architecture
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_OPENAL_ARCH "x64")
    else()
        set(_OPENAL_ARCH "x86")
    endif()
    set(_OPENAL_DLL "${sfml_SOURCE_DIR}/extlibs/bin/${_OPENAL_ARCH}/openal32.dll")
    message(STATUS "OpenAL DLL: ${_OPENAL_DLL} (arch=${_OPENAL_ARCH}, sizeof(void*)=${CMAKE_SIZEOF_VOID_P})")
    if(EXISTS "${_OPENAL_DLL}")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_OPENAL_DLL}"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
        )
    else()
        message(WARNING "OpenAL DLL not found at ${_OPENAL_DLL} – SFML Audio may fail at runtime!")
    endif()
endif()

# ─── Tests ────────────────────────────────────────────────────────────────────
if(IS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ─── Install ──────────────────────────────────────────────────────────────────
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
install(DIRECTORY assets/ DESTINATION bin/assets)
install(DIRECTORY web/out/ DESTINATION bin/dashboard)
install(DIRECTORY config/ DESTINATION bin/config)
