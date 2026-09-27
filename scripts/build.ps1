# Configures, builds and tests the production code with MSVC + Ninja (via vcvars64).
#   scripts\build.ps1            Release build + tests
#   scripts\build.ps1 -Clean     start from an empty build directory
param([switch]$Clean, [string]$Config = 'Release')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$bin = Join-Path $root 'build'
$vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat'

if ($Clean -and (Test-Path $bin)) { Remove-Item $bin -Recurse -Force }
cmd /c "`"$vcvars`" >nul && cmake -S `"$root`" -B `"$bin`" -G Ninja -DCMAKE_BUILD_TYPE=$Config && cmake --build `"$bin`" && ctest --test-dir `"$bin`" --output-on-failure"
exit $LASTEXITCODE
