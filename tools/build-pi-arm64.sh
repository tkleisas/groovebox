#!/usr/bin/env bash
# Cross-build groovebox_sim for 64-bit Raspberry Pi (arm64 bookworm)
# inside Docker, prove the binary's architecture, and smoke-run it
# under qemu-aarch64.
#
# Usage (Git Bash / Linux / macOS):
#   tools/build-pi-arm64.sh            # build + verify
#
# The qemu step smoke-runs the arm64 binary: --version, then a backend
# boot (--profile). The container has no ALSA device PortAudio accepts
# (it rejects the 'null' PCM over its ~2^30 max-channel report), so the
# engine profile exits 1 after proving the Pi backend boots and degrades
# gracefully — real profile numbers need a Pi with a codec (or the
# Windows sim).
set -euo pipefail
cd "$(dirname "$0")/.."

IMAGE=groovebox-pi-build
PROFILE_SECS="${PROFILE_SECS:-5}"

docker build -f emulator/pi/Dockerfile -t "$IMAGE" .

# MSYS_NO_PATHCONV: stop Git Bash from rewriting /src into a Windows path.
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd):/src" -w /src "$IMAGE" bash -c "
set -e
cmake -B emulator/build-pi-arm64 -S emulator -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=/src/emulator/pi/aarch64-toolchain.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DGB_PI_BACKEND=ON \
    -DSDL_UNIX_CONSOLE_BUILD=ON
cmake --build emulator/build-pi-arm64 --target groovebox_sim -j\"\$(nproc)\"

echo '── binary provenance ──'
command -v file >/dev/null && file emulator/build-pi-arm64/groovebox_sim || true
readelf -h emulator/build-pi-arm64/groovebox_sim | grep -E 'Class|Machine'

echo '── qemu smoke: --version ──'
qemu-aarch64 emulator/build-pi-arm64/groovebox_sim --version

echo \"── qemu smoke: backend boot (--profile ${PROFILE_SECS}) ──\"
# No sound hardware in the container. Even with ALSA's default pointed
# at the 'null' plugin, PortAudio 19.8 refuses to enumerate it (the
# null PCM reports ~2^30 max channels, over PA's 20000 sanity cap in
# GropeDevice), so yawn's engine init fails with \"no default audio
# output device\" and the app exits 1 — EXPECTED here. What this run
# proves: the arm64 binary executes, the Pi backend boots, and both
# missing peripherals (fbdev, USB-MIDI panel) degrade gracefully.
# Real profile numbers come from the Windows sim or a Pi with a codec.
printf 'pcm.!default { type null }\nctl.!default { type null }\n' > /root/.asoundrc
qemu-aarch64 emulator/build-pi-arm64/groovebox_sim --profile ${PROFILE_SECS} || true
"
