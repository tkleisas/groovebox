# CMake toolchain: Debian bookworm x86_64 container -> aarch64 (Pi 4/5,
# 64-bit Raspberry Pi OS bookworm). Compilers come from
# gcc-aarch64-linux-gnu; target libraries from dpkg multiarch
# (libasound2-dev:arm64 installs into /usr/lib/aarch64-linux-gnu).
#
# Only the arm64 variants of the audio dev packages are installed in
# the build image, so find_library/find_path re-rooted below cannot
# pick up an x86_64 lib by accident.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# /usr/aarch64-linux-gnu: cross-compiler sysroot bits.
# /usr: multiarch target libs/headers (lib/aarch64-linux-gnu,
#       include/aarch64-linux-gnu) — CMake appends the arch triplet
#       automatically when searching under this prefix.
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu /usr)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)  # host tools (pkg-config, python)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
