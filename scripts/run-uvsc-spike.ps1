# Builds spikes/uvsc with MSVC/CMake/Ninja (vcvars64) and runs it against the bit-band test
# snapshot: run-to delay() N times, then to BarrelShift. Addresses come from the map.
param([int]$Stops = 6, [switch]$NoBuild)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$fw  = Join-Path $root 'reference\bitband-test'
$log  = Join-Path $env:TEMP 'vwb-sim.log'   # LOG > rejects paths with spaces
$bin  = Join-Path $root 'build\uvsc-spike'   # separate from the production build tree

if (-not $NoBuild) {
    $vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
    cmd /c "`"$vcvars`" >nul && cmake -S `"$root`" -B `"$bin`" -G Ninja -DCMAKE_BUILD_TYPE=Release -DVWB_BUILD_UVSC_SPIKE=ON -DVWB_BUILD_TESTS=OFF >nul && cmake --build `"$bin`" --target uvsc_spike"
    if ($LASTEXITCODE) { throw 'build failed' }
}

function Addr($sym) {
    $m = Select-String "$fw\Listings\Bitband.map" -Pattern "^\s+$sym\s+0x([0-9a-fA-F]+)\s+Thumb Code" | Select-Object -First 1
    if (-not $m) { throw "symbol $sym not in map" }
    '{0:X}' -f ([Convert]::ToUInt32($m.Matches[0].Groups[1].Value, 16) -band 0xFFFFFFFE)   # clear Thumb bit
}
$delay = Addr 'delay'; $final = Addr 'BarrelShift'

# Breakpoints saved into the project by an earlier session come back after the INI
# runs; reset to Keil's empty form so this run starts clean (see run-sim.ps1).
$opt = "$fw\Bitband.uvoptx"
[IO.File]::WriteAllText($opt, ([regex]::Replace([IO.File]::ReadAllText($opt), '<Breakpoint>[\s\S]*?</Breakpoint>', '<Breakpoint/>')))

Remove-Item $log -EA SilentlyContinue
(Get-Content "$root\spikes\uvsc\bitband-observe.ini" -Raw).Replace('%LOG%', $log) | Set-Content "$fw\sim.ini" -Encoding ascii
"uvsc_spike $fw\Bitband.uvprojx $delay $Stops $final"
& "$bin\uvsc_spike.exe" "$fw\Bitband.uvprojx" $delay $Stops $final
$rc = $LASTEXITCODE
'--- INI log'
if (Test-Path $log) { Get-Content $log | Select-String '^SNAP|\*\*\*' } else { '(no INI log)' }
exit $rc
