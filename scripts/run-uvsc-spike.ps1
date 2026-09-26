# Builds spikes/uvsc with MSVC/CMake/Ninja (vcvars64) and runs it against the bit-band test
# snapshot: run-to delay() N times, then to BarrelShift. Addresses come from the map.
param([int]$Stops = 6, [switch]$NoBuild)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$fw  = Join-Path $root 'reference\bitband-test'
$log  = Join-Path $env:TEMP 'vwb-sim.log'   # LOG > rejects paths with spaces

if (-not $NoBuild) {
    $vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
    cmd /c "`"$vcvars`" >nul && cmake -S `"$root`" -B `"$root\build`" -G Ninja -DCMAKE_BUILD_TYPE=Release >nul && cmake --build `"$root\build`"" 
    if ($LASTEXITCODE) { throw 'build failed' }
}

function Addr($sym) {
    $m = Select-String "$fw\Listings\Bitband.map" -Pattern "^\s+$sym\s+0x([0-9a-fA-F]+)\s+Thumb Code" | Select-Object -First 1
    if (-not $m) { throw "symbol $sym not in map" }
    '{0:X}' -f ([Convert]::ToUInt32($m.Matches[0].Groups[1].Value, 16) -band 0xFFFFFFFE)   # clear Thumb bit
}
$delay = Addr 'delay'; $final = Addr 'BarrelShift'

Remove-Item $log -EA SilentlyContinue
(Get-Content "$root\spikes\uvsc\bitband-observe.ini" -Raw).Replace('%LOG%', $log) | Set-Content "$fw\sim.ini" -Encoding ascii
"uvsc_spike $fw\Bitband.uvprojx $delay $Stops $final"
& "$root\build\uvsc_spike.exe" "$fw\Bitband.uvprojx" $delay $Stops $final
$rc = $LASTEXITCODE
'--- INI log'
if (Test-Path $log) { Get-Content $log | Select-String '^SNAP|\*\*\*' } else { '(no INI log)' }
exit $rc
