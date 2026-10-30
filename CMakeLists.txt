# crackdown2 - ReXGlue Recompiled Project
#
# This file is yours to edit. 'rexglue migrate' will NOT overwrite it.
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(crackdown2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(CRACKDOWN2_SOURCES
    src/main.cpp
    src/hooks.cpp
    src/native_menu.cpp
    src/game_console.cpp
    src/profiling.cpp
)

if(WIN32)
    add_executable(crackdown2 WIN32 ${CRACKDOWN2_SOURCES})
elseif(ANDROID)
    # SDLActivity loads libmain.so and calls its SDL_main; the generated code
    # and the SDK runtime come along as shared libraries in the APK.
    add_library(crackdown2 SHARED ${CRACKDOWN2_SOURCES})
    set_target_properties(crackdown2 PROPERTIES OUTPUT_NAME main)
else()
    add_executable(crackdown2 ${CRACKDOWN2_SOURCES})
endif()

rexglue_setup_target(crackdown2 GPU_PLUGINS xenos)

# In-tree SDK builds (Android) do not export the ImGui/RenderDoc include
# directories that rex/rex_app.h needs; the installed SDK ships them.
if(TARGET imgui)
    target_link_libraries(crackdown2 PRIVATE imgui::imgui)
endif()
if(TARGET renderdoc)
    target_link_libraries(crackdown2 PRIVATE renderdoc)
endif()


# Windows application/taskbar icon (resources/crackdown2.rc -> crackdown2.ico).
if(WIN32)
    enable_language(RC)
    target_sources(crackdown2 PRIVATE resources/crackdown2.rc)
endif()
