# Cross-build image for the groovebox Pi backend (arm64 bookworm).
#
#   docker build -f emulator/pi/Dockerfile -t groovebox-pi-build .
#
# The image carries the aarch64 cross toolchain plus the TARGET-arch
# (arm64) development packages for the audio deps that yawn compiles
# from source: RtMidi and PortAudio both need ALSA headers + libs for
# aarch64. Only the :arm64 variants are installed so CMake's library
# search (re-rooted by pi/aarch64-toolchain.cmake) cannot resolve to
# an x86_64 .so by accident. python3 + jinja2 are needed at configure
# time only — yawn's Dependencies.cmake invokes glad's code generator
# even though the emulator never compiles the GL target.
#
# Two yawn-cmake configure gates are sidestepped WITHOUT patching yawn:
#   * SDL3 hard-errors its configure when no X11/Wayland dev libs exist
#     — bypassed with -DSDL_UNIX_CONSOLE_BUILD=ON (SDL's documented
#     headless/console opt-out) in tools/build-pi-arm64.sh.
#   * yawn/CMakeLists.txt find_package(OpenGL REQUIRED) is unconditional
#     on Linux for the desktop yawn exe (which we never compile) —
#     satisfied here by the arm64 mesa GL dev packages. Nothing links
#     against GL in the emulator binary.
FROM debian:bookworm

RUN dpkg --add-architecture arm64 \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
        build-essential \
        gcc-aarch64-linux-gnu \
        g++-aarch64-linux-gnu \
        cmake \
        ninja-build \
        git \
        ca-certificates \
        pkg-config \
        python3 \
        python3-jinja2 \
        libasound2-dev:arm64 \
        libgl1-mesa-dev:arm64 \
        libopengl-dev:arm64 \
        qemu-user \
 && rm -rf /var/lib/apt/lists/*

# Point pkg-config at the arm64 .pc files (the host copies in
# /usr/lib/x86_64-linux-gnu/pkgconfig describe the wrong arch).
ENV PKG_CONFIG_PATH=/usr/lib/aarch64-linux-gnu/pkgconfig

WORKDIR /src
