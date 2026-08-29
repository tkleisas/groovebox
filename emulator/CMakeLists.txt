cmake_minimum_required(VERSION 3.24)
project(groovebox_sim LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── yawn engine: slim headless build ────────────────────────────────
# All optional features off — the emulator needs only the audio core.
set(YAWN_VST3 OFF CACHE BOOL "" FORCE)
set(YAWN_HAS_NAM OFF CACHE BOOL "" FORCE)
set(YAWN_HAS_LINK OFF CACHE BOOL "" FORCE)
set(YAWN_HAS_MODEL3D OFF CACHE BOOL "" FORCE)
set(YAWN_HAS_BASIC_PITCH OFF CACHE BOOL "" FORCE)
set(YAWN_HAS_STEM_SEPARATION OFF CACHE BOOL "" FORCE)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../yawn yawn-build)

add_executable(groovebox_sim
    src/main.cpp
    src/SimBackend.cpp
    src/PanelView.cpp
    src/ui.cpp
    src/widgets.cpp
    src/widgetshowcase.cpp
    src/fontchart.cpp
    # With YAWN_HAS_LINK=OFF, yawn/CMakeLists.txt does not compile
    # src/link/LinkManager.cpp, but AudioEngine.cpp references the
    # LinkManager stub unconditionally. Compile that same file here
    # (without YAWN_HAS_LINK it builds the no-op stub) so yawn_core
    # links cleanly. This does NOT modify anything inside yawn.
    ${CMAKE_CURRENT_SOURCE_DIR}/../yawn/src/link/LinkManager.cpp
)

# yawn_core exports yawn/src as a PUBLIC include dir, but be explicit.
target_include_directories(groovebox_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../yawn/src
)

target_link_libraries(groovebox_sim PRIVATE
    yawn_core
    SDL3::SDL3
)

# Emulator sources get their own (milder) warning level — yawn's /W4
# promote-to-error gate is applied per-target by yawn itself and does
# not leak into this target.
if(MSVC)
    target_compile_options(groovebox_sim PRIVATE /W3 /utf-8)
endif()

# PortAudio builds SHARED on Windows; its DLL lands in yawn's
# CMAKE_RUNTIME_OUTPUT_DIRECTORY (build/bin), not next to our exe.
# Copy it post-build so groovebox_sim starts from its own directory.
if(TARGET portaudio)
    get_target_property(_pa_type portaudio TYPE)
    if(_pa_type STREQUAL "SHARED_LIBRARY")
        add_custom_command(TARGET groovebox_sim POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:portaudio> $<TARGET_FILE_DIR:groovebox_sim>
            VERBATIM)
    endif()
endif()
