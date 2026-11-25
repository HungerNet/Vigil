cmake_minimum_required(VERSION 3.20)
project(Vigil VERSION 2.0.0 LANGUAGES CXX RC)

option(VIGIL_BUILD_DLL "Build Vigil.dll (requires JNI headers from a JDK)" ON)
option(VIGIL_BUILD_LAUNCHER "Build VigilLauncher.exe" ON)
option(VIGIL_BUILD_PREVIEW "Build the standalone UI preview harness" ON)
option(VIGIL_BUILD_TESTS "Build the detection test suite" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /MP /wd4100 /wd4505)
    add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

# ── Core + detection framework (no JNI, no OpenGL) ───────────────────────────────────────────
add_library(vigil_core STATIC
    src/core/json.cpp
    src/core/log.cpp
    src/core/settings.cpp
    src/detection/check.cpp
    src/detection/combat.cpp
    src/detection/confidence.cpp
    src/detection/engine.cpp
    src/detection/session.cpp
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
target_include_directories(vigil_core PUBLIC src)

# ── User interface (OpenGL fixed function + GDI typography) ──────────────────────────────────
add_library(vigil_ui STATIC
    src/ui/app.cpp
    src/ui/font.cpp
    src/ui/gfx.cpp
    src/ui/icons.cpp
    src/ui/page_checks.cpp
    src/ui/page_common.cpp
    src/ui/page_evidence.cpp
    src/ui/page_overview.cpp
    src/ui/page_players.cpp
    src/ui/page_settings.cpp
    src/ui/theme.cpp
    src/ui/widgets.cpp
)
target_link_libraries(vigil_ui PUBLIC vigil_core opengl32 gdi32 user32)

# ── Vigil.dll ────────────────────────────────────────────────────────────────────────────────
if(VIGIL_BUILD_DLL)
    find_package(JNI REQUIRED)
    add_library(Vigil SHARED
        src/platform/dllmain.cpp
        src/platform/hooks.cpp
        src/game/alerts.cpp
        src/game/class_finder.cpp
        src/game/context.cpp
        src/game/jvm.cpp
        src/game/mc.cpp
        src/game/minecraft_util.cpp
        src/game/runtime.cpp
        src/game/sampler.cpp
        src/game/world_render.cpp
        assets/vigil.rc
    )
    target_include_directories(Vigil PRIVATE ${JNI_INCLUDE_DIRS})
    target_link_libraries(Vigil PRIVATE vigil_ui shell32)
endif()

# ── VigilLauncher.exe ────────────────────────────────────────────────────────────────────────
if(VIGIL_BUILD_LAUNCHER)
    add_executable(VigilLauncher WIN32 src/platform/launcher.cpp assets/launcher.rc)
endif()

# ── Preview harness ──────────────────────────────────────────────────────────────────────────
if(VIGIL_BUILD_PREVIEW)
    add_executable(VigilPreview tools/preview/preview.cpp assets/vigil.rc)
    target_link_libraries(VigilPreview PRIVATE vigil_ui)
endif()

# ── Tests ────────────────────────────────────────────────────────────────────────────────────
if(VIGIL_BUILD_TESTS)
    enable_testing()
    add_executable(detection_tests tests/detection_tests.cpp)
    target_link_libraries(detection_tests PRIVATE vigil_core)
    add_test(NAME detection COMMAND detection_tests)
endif()
