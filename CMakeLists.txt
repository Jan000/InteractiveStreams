cmake_minimum_required(VERSION 3.20)
project(InteractiveStreams VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

# ─── Dependencies via FetchContent ────────────────────────────────────────────
include(FetchContent)

# SFML 2.6 – Rendering & Window
FetchContent_Declare(
    sfml
    GIT_REPOSITORY https://github.com/SFML/SFML.git
    GIT_TAG        2.6.1
    GIT_SHALLOW    TRUE
)

# Box2D – Physics
FetchContent_Declare(
    box2d
    GIT_REPOSITORY https://github.com/erincatto/box2d.git
    GIT_TAG        v2.4.1
    GIT_SHALLOW    TRUE
)
set(BOX2D_BUILD_TESTBED OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)

# nlohmann/json – JSON handling
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)

# cpp-httplib – Embedded HTTP server for admin dashboard
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.15.3
    GIT_SHALLOW    TRUE
)

# spdlog – Logging
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.13.0
    GIT_SHALLOW    TRUE
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
cmake_policy(SET CMP0169 OLD)
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

# ─── Source files ─────────────────────────────────────────────────────────────
set(CORE_SOURCES
    src/core/Application.cpp
    src/core/Config.cpp
    src/core/GameManager.cpp
    src/core/Logger.cpp
    src/core/ChannelManager.cpp
    src/core/StreamInstance.cpp
    src/core/StreamManager.cpp
    src/core/PlayerDatabase.cpp
    src/core/PerfMonitor.cpp
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
)

set(PLATFORM_SOURCES
    src/platform/local/LocalPlatform.cpp
    src/platform/twitch/TwitchPlatform.cpp
    src/platform/youtube/YoutubePlatform.cpp
)

set(RENDERING_SOURCES
    src/rendering/Renderer.cpp
    src/rendering/Background.cpp
    src/rendering/PostProcessing.cpp
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
    box2d
    nlohmann_json::nlohmann_json
    httplib::httplib
    spdlog::spdlog
    sqlite3_lib
)

# ─── Speed: Precompiled Header ────────────────────────────────────────────────
# Alle schweren Headers (SFML, Box2D, nlohmann/json, spdlog, STL) werden einmal
# vorkompiliert. Das spart bei inkrementellen Builds mehrere Sekunden pro TU.
target_precompile_headers(${PROJECT_NAME} PRIVATE src/pch.h)

# Copy assets to build directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets
)

# Copy web dashboard to build directory (Next.js static export)
# Build with: cd web && bun run build   (produces web/out/)
if(EXISTS ${CMAKE_SOURCE_DIR}/web/out)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/web/out $<TARGET_FILE_DIR:${PROJECT_NAME}>/dashboard
    )
else()
    message(WARNING "web/out/ not found – dashboard will not be available. Run: cd web && bun run build")
endif()

# Copy config to build directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/config $<TARGET_FILE_DIR:${PROJECT_NAME}>/config
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
    # OpenAL DLL (required by SFML Audio)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/build/_deps/sfml-src/extlibs/bin/x64/openal32.dll
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
endif()

# ─── Tests ────────────────────────────────────────────────────────────────────
if(IS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ─── Install ──────────────────────────────────────────────────────────────────
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
install(DIRECTORY assets/ DESTINATION bin/assets)
if(EXISTS ${CMAKE_SOURCE_DIR}/web/out/)
    install(DIRECTORY web/out/ DESTINATION bin/dashboard)
endif()
install(DIRECTORY config/ DESTINATION bin/config)
