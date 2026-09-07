cmake_minimum_required(VERSION 3.24)
project(groovebox_sim LANGUAGES C CXX)

# ── Version from git describe (configure-time) ──────────────────────
# Runs 'git describe --tags --always --dirty' from the repo ROOT (the
# emulator is a subdirectory of it). A 'vX.Y.Z' tag yields "X.Y.Z";
# anything else (no tags yet, mid-history, dirty tree) is reported as
# "0.0.0-<describe output>". NOTE: the version is captured at CONFIGURE
# time — re-run cmake after tagging/committing to refresh it.
set(GB_VERSION "0.0.0-unknown")
execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    OUTPUT_VARIABLE GB_GIT_DESCRIBE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE GB_GIT_RESULT
)
if(GB_GIT_RESULT EQUAL 0 AND GB_GIT_DESCRIBE)
    if(GB_GIT_DESCRIBE MATCHES "^v?([0-9]+\\.[0-9]+\\.[0-9]+)")
        set(GB_VERSION "${CMAKE_MATCH_1}")
    else()
        set(GB_VERSION "0.0.0-${GB_GIT_DESCRIBE}")
    endif()
endif()
message(STATUS "groovebox_sim version: ${GB_VERSION}")

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

# ── Backend selection ───────────────────────────────────────────────
# OFF (default): SimBackend — SDL3 window + PC keyboard/mouse.
# ON:  PiBackend — headless Raspberry Pi (fbdev /dev/fb1|fb0 display,
#      RtMidi USB panel, no SDL3 in the emulator binary). RtMidi +
#      ALSA come transitively from yawn_core's PUBLIC link.
option(GB_PI_BACKEND "Headless Raspberry Pi backend (fbdev + RtMidi)" OFF)
if(GB_PI_BACKEND)
    set(GB_BACKEND_SRC src/PiBackend.cpp)
else()
    set(GB_BACKEND_SRC src/SimBackend.cpp)
endif()

add_executable(groovebox_sim
    src/main.cpp
    ${GB_BACKEND_SRC}
    src/PanelView.cpp
    src/PanelViewNSR2.cpp
    src/ui.cpp
    src/widgets.cpp
    src/widgetshowcase.cpp
    src/fontchart.cpp
    src/sampleio.cpp
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
)
# SDL3 is only needed by the SimBackend (window, synthetic probe
# events). The Pi build links RtMidi/ALSA transitively via yawn_core.
if(NOT GB_PI_BACKEND)
    target_link_libraries(groovebox_sim PRIVATE SDL3::SDL3)
else()
    target_compile_definitions(groovebox_sim PRIVATE GB_PI_BACKEND)
endif()

# Emulator sources get their own (milder) warning level — yawn's /W4
# promote-to-error gate is applied per-target by yawn itself and does
# not leak into this target.
if(MSVC)
    target_compile_options(groovebox_sim PRIVATE /W3 /utf-8)
endif()

# Version string from git describe (see top of file).
target_compile_definitions(groovebox_sim PRIVATE
    GB_VERSION_STRING="${GB_VERSION}")

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
