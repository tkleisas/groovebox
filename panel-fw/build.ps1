# Build helper for the groovebox panel firmware.
# Expects: ARM toolchain + ninja under %USERPROFILE%\pico-tools, pico-sdk at
# %USERPROFILE%\pico-tools\pico-sdk (see README.md). Host (pioasm) builds
# use the Visual Studio toolchain via vcvars64.
#
#   .\build.ps1 -Configure   # first run / after CMake edits
#   .\build.ps1              # incremental build
#   .\build.ps1 -Clean       # wipe build dir first

param(
    [switch]$Configure,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$tools = Join-Path $env:USERPROFILE 'pico-tools'

if (-not (Test-Path (Join-Path $tools 'pico-sdk\pico_sdk_init.cmake'))) {
    throw ('pico-sdk not found at ' + $tools + '\pico-sdk - see README.md')
}

$env:PICO_SDK_PATH = Join-Path $tools 'pico-sdk'

# CMake 4.x (bundled with VS 18) dropped <3.5 compatibility; the SDK 1.5.x
# pioasm tool declares an old minimum, so give it the documented escape hatch.
$env:CMAKE_POLICY_VERSION_MINIMUM = '3.5'

$tc = Get-ChildItem $tools -Directory -Filter 'arm-gnu-toolchain-*' | Select-Object -First 1
if (-not $tc) { throw ('ARM toolchain not found under ' + $tools) }
$env:PATH = (Join-Path $tc.FullName 'bin') + ';' + (Join-Path $tools 'ninja') + ';' + $env:PATH

$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -property installationPath
if (-not $vs) { throw 'Visual Studio not found via vswhere' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw ('vcvars64.bat not found at ' + $vcvars) }

if ($Clean -and (Test-Path (Join-Path $root 'build'))) {
    Remove-Item -Recurse -Force (Join-Path $root 'build')
}

$cmakeArgs = '-S ' + $root + ' -B ' + (Join-Path $root 'build') + ' -G Ninja -DCMAKE_BUILD_TYPE=Release'
$call = 'call "' + $vcvars + '" >nul 2>&1 && '

if ($Configure -or -not (Test-Path (Join-Path $root 'build\CMakeCache.txt'))) {
    cmd /c ($call + 'cmake ' + $cmakeArgs)
    if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed' }
}
cmd /c ($call + 'cmake --build ' + (Join-Path $root 'build'))
if ($LASTEXITCODE -ne 0) { throw 'build failed' }

Write-Host ''
Write-Host 'OK -> ' (Join-Path $root 'build\panel.uf2')
