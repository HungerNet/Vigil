cmake_minimum_required(VERSION 3.20)

# ── Version: version.yaml is the single source ("1.0-dev.9" → 1.0.9) ─────────────────────────
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/version.yaml" VIGIL_VERSION_YAML)
string(REGEX MATCH "version:[ ]*'?([^'\r\n]+)'?" _ "${VIGIL_VERSION_YAML}")
set(VIGIL_VERSION_STRING "${CMAKE_MATCH_1}")
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)" _ "${VIGIL_VERSION_STRING}")
set(VIGIL_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(VIGIL_VERSION_MINOR "${CMAKE_MATCH_2}")
string(REGEX MATCH "([0-9]+)$" _ "${VIGIL_VERSION_STRING}")
set(VIGIL_VERSION_PATCH "${CMAKE_MATCH_1}")
if(NOT VIGIL_VERSION_MAJOR)
    set(VIGIL_VERSION_MAJOR 0)
    set(VIGIL_VERSION_MINOR 0)
endif()
if(NOT VIGIL_VERSION_PATCH)
    set(VIGIL_VERSION_PATCH 0)
endif()

# ── Build ID: ties Vigil.exe to the Vigil.dll it embeds (docs/RELEASE_PROCESS.md) ─────────────
# <commit>.<UTC configure time>. CI passes VIGIL_BUILD_COMMIT; locally git is asked.
if(DEFINED ENV{GITHUB_SHA})
    string(SUBSTRING "$ENV{GITHUB_SHA}" 0 10 VIGIL_BUILD_COMMIT)
else()
    execute_process(COMMAND git rev-parse --short=10 HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                    OUTPUT_VARIABLE VIGIL_BUILD_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
if(NOT VIGIL_BUILD_COMMIT)
    set(VIGIL_BUILD_COMMIT "local")
endif()
string(TIMESTAMP VIGIL_BUILD_TIME "%Y%m%d%H%M%S" UTC)
set(VIGIL_BUILD_ID "${VIGIL_BUILD_COMMIT}.${VIGIL_BUILD_TIME}")

project(Vigil VERSION ${VIGIL_VERSION_MAJOR}.${VIGIL_VERSION_MINOR}.${VIGIL_VERSION_PATCH} LANGUAGES CXX RC)

option(VIGIL_BUILD_DLL "Build Vigil.dll and Vigil.exe (requires JNI headers from a JDK)" ON)
option(VIGIL_BUILD_PREVIEW "Build the standalone UI preview harness" ON)
option(VIGIL_BUILD_TESTS "Build the test suites and the trace replay tool" ON)
option(VIGIL_WARNINGS_AS_ERRORS "Treat compiler warnings as errors (CI)" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/W4> $<$<COMPILE_LANGUAGE:CXX>:/permissive->
                        $<$<COMPILE_LANGUAGE:CXX>:/utf-8> $<$<COMPILE_LANGUAGE:CXX>:/MP>
                        $<$<COMPILE_LANGUAGE:CXX>:/wd4100> $<$<COMPILE_LANGUAGE:CXX>:/wd4505>)
    if(VIGIL_WARNINGS_AS_ERRORS)
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/WX>)
    endif()
    add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

set(VIGIL_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
configure_file(src/core/version.h.in "${VIGIL_GENERATED_DIR}/core/version.h" @ONLY)
include_directories("${VIGIL_GENERATED_DIR}")

# ── Libraries ────────────────────────────────────────────────────────────────────────────────
# The split is a dependency rule, enforced by the linker (docs/ARCHITECTURE.md):
#   vigil_core       json, logging, paths, settings registry          (std + shell32 for paths)
#   vigil_detection  the detection engine: pure C++, depends only on vigil_core
#   vigil_services   identity tools (network), utilities logic        (depends on vigil_core)
#   vigil_ui         OpenGL interface                                  (all of the above)
# vigil_detection is the first candidate for a Rust port (RUST_PLAN.md); nothing in it may
# include JNI, OpenGL, WinHTTP or <windows.h>.
add_library(vigil_core STATIC
    src/core/json.cpp
    src/core/log.cpp
    src/core/paths.cpp
    src/core/settings.cpp
)
target_include_directories(vigil_core PUBLIC src "${VIGIL_GENERATED_DIR}")
target_link_libraries(vigil_core PUBLIC shell32 ole32)

add_library(vigil_detection STATIC
    src/detection/check.cpp
    src/detection/combat.cpp
    src/detection/confidence.cpp
    src/detection/engine.cpp
    src/detection/marks.cpp
    src/detection/match.cpp
    src/detection/roster.cpp
    src/detection/replay.cpp
    src/detection/session.cpp
    src/detection/snapshot.cpp
    src/detection/trace.cpp
    src/detection/track.cpp
    src/detection/checks/autoblock.cpp
    src/detection/checks/autoclicker.cpp
    src/detection/checks/bhop.cpp
    src/detection/checks/killaura.cpp
    src/detection/checks/noslow.cpp
    src/detection/checks/reach.cpp
    src/detection/checks/registry.cpp
    src/detection/checks/scaffold.cpp
    src/detection/checks/speed.cpp
    src/detection/checks/velocity.cpp
)
target_link_libraries(vigil_detection PUBLIC vigil_core)

add_library(vigil_services STATIC
    src/net/http.cpp
    src/identity/identity.cpp
    src/utilities/utilities.cpp
)
target_link_libraries(vigil_services PUBLIC vigil_core winhttp)

# ── User interface (OpenGL fixed function + GDI typography) ──────────────────────────────────
add_library(vigil_ui STATIC
    src/ui/app.cpp
    src/ui/font.cpp
    src/ui/gfx.cpp
    src/ui/hud.cpp
    src/ui/icons.cpp
    src/ui/onboarding.cpp
    src/ui/page_checks.cpp
    src/ui/page_common.cpp
    src/ui/page_evidence.cpp
    src/ui/page_overview.cpp
    src/ui/page_players.cpp
    src/ui/page_records.cpp
    src/ui/page_settings.cpp
    src/ui/page_status.cpp
    src/ui/page_utilities.cpp
    src/ui/records.cpp
    src/ui/search.cpp
    src/ui/sound.cpp
    src/ui/suspicion.cpp
    src/ui/theme.cpp
    src/ui/widgets.cpp
)
target_link_libraries(vigil_ui PUBLIC vigil_detection vigil_services opengl32 gdi32 user32 winmm)

# ── Vigil.dll (runtime injected into the game) and Vigil.exe (the only file users run) ───────
if(VIGIL_BUILD_DLL)
    find_package(JNI REQUIRED)
    add_library(Vigil SHARED
        src/platform/dllmain.cpp
        src/platform/hooks.cpp
        src/platform/runtime_guard.cpp
        src/game/alerts.cpp
        src/game/chat.cpp
        src/game/class_finder.cpp
        src/game/context.cpp
        src/game/jvm.cpp
        src/game/mc.cpp
        src/game/minecraft_util.cpp
        src/game/runtime.cpp
        src/game/sampler.cpp
        src/game/utilities_runtime.cpp
        src/game/world_render.cpp
        assets/vigil.rc
    )
    target_include_directories(Vigil PRIVATE ${JNI_INCLUDE_DIRS})
    target_link_libraries(Vigil PRIVATE vigil_ui bcrypt version)

    # Vigil.exe embeds Vigil.dll as a plain (uncompressed, unencrypted) resource and writes it to
    # a fixed, versioned path at run time. The embedding script is generated so it can name the
    # DLL's build location for any generator/configuration.
    set(VIGIL_EMBED_RC "${VIGIL_GENERATED_DIR}/launcher_embed_$<CONFIG>.rc")
    file(GENERATE OUTPUT "${VIGIL_EMBED_RC}"
         CONTENT "// Generated: Vigil.dll as resource 301 (VIGIL_RES_EMBEDDED_DLL in src/ui/resources.h).\n301 RCDATA \"$<TARGET_FILE:Vigil>\"\n")
    add_executable(VigilApp WIN32 src/platform/launcher.cpp src/platform/runtime_guard.cpp assets/launcher.rc "${VIGIL_EMBED_RC}")
    set_target_properties(VigilApp PROPERTIES OUTPUT_NAME Vigil)
    target_link_libraries(VigilApp PRIVATE vigil_ui dwmapi bcrypt version)
    target_include_directories(VigilApp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_dependencies(VigilApp Vigil)
    set_source_files_properties("${VIGIL_EMBED_RC}" PROPERTIES GENERATED TRUE OBJECT_DEPENDS "$<TARGET_FILE:Vigil>")
    if(MSVC)
        target_link_options(VigilApp PRIVATE /MANIFEST:NO)
    endif()
endif()

# ── Preview harness ──────────────────────────────────────────────────────────────────────────
if(VIGIL_BUILD_PREVIEW)
    add_executable(VigilPreview tools/preview/preview.cpp assets/vigil.rc)
    target_link_libraries(VigilPreview PRIVATE vigil_ui)
endif()

# ── Tests and the trace replay tool ──────────────────────────────────────────────────────────
if(VIGIL_BUILD_TESTS)
    enable_testing()
    add_executable(detection_tests tests/detection_tests.cpp)
    target_link_libraries(detection_tests PRIVATE vigil_detection)
    add_test(NAME detection COMMAND detection_tests)

    add_executable(feature_tests tests/feature_tests.cpp)
    target_link_libraries(feature_tests PRIVATE vigil_services)
    add_test(NAME features COMMAND feature_tests)

    add_executable(records_tests tests/records_tests.cpp)
    target_link_libraries(records_tests PRIVATE vigil_detection)
    add_test(NAME records COMMAND records_tests)

    add_executable(validation_tests tests/validation_tests.cpp)
    target_link_libraries(validation_tests PRIVATE vigil_detection)
    add_test(NAME validation COMMAND validation_tests)
    # Golden traces are read from the source tree (tests/traces).
    target_compile_definitions(validation_tests PRIVATE VIGIL_TRACE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/traces")

    add_executable(ui_logic_tests tests/ui_logic_tests.cpp)
    target_link_libraries(ui_logic_tests PRIVATE vigil_ui)
    add_test(NAME ui_logic COMMAND ui_logic_tests)

    add_executable(VigilReplay tools/replay/replay.cpp)
    target_link_libraries(VigilReplay PRIVATE vigil_detection)
endif()
