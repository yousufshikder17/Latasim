# Recreates reference/blinky from the installed LPC1700_DFP and builds it.
# The pack copy is never modified. The only changes to the project copy are the
# debugger settings, so the Debug target runs in the µVision simulator:
#   uSim=1/uTrg=0            simulator instead of ULINK2 (stock is hardware)
#   sIfile=.\sim.ini         debug init script, written per experiment
#   SimDlgDll/Args           -Peripherals LPC (default): DARMP1.DLL -pLPC1768,
#                            the values Keil's own MCB1700\Blinky_ULp project uses
#                            -Peripherals Core: keeps the stock DCM.DLL -pCM3
param([ValidateSet('LPC','Core')][string]$Peripherals = 'LPC')
$ErrorActionPreference = 'Stop'

$src  = "$env:LOCALAPPDATA\Arm\Packs\Keil\LPC1700_DFP\2.6.0\Boards\Keil\MCB1700\Blinky"
$root = Split-Path $PSScriptRoot
$dst  = Join-Path $root 'reference\blinky'
$uv4  = 'C:\Keil_v5\UV4\UV4.exe'

if (Test-Path $dst) { Remove-Item $dst -Recurse -Force }
Copy-Item $src $dst -Recurse
Remove-Item "$dst\Blinky.uvguix*" -EA SilentlyContinue   # per-user GUI state
Get-ChildItem $dst -Recurse -File | ForEach-Object { $_.IsReadOnly = $false }  # pack files ship read-only

# Only the first <Target> (Debug) is patched: it is the first match in each file.
function Patch-First($file, $pattern, $replacement) {
    $text = [IO.File]::ReadAllText($file)
    $re = [regex]$pattern
    if (-not $re.IsMatch($text)) { throw "pattern not found in ${file}: $pattern" }
    [IO.File]::WriteAllText($file, $re.Replace($text, $replacement, 1))
}
$opt = "$dst\Blinky.uvoptx"; $prj = "$dst\Blinky.uvprojx"
Patch-First $opt '<uSim>0</uSim>' '<uSim>1</uSim>'
Patch-First $opt '<uTrg>1</uTrg>' '<uTrg>0</uTrg>'
Patch-First $opt '<sIfile></sIfile>' '<sIfile>.\sim.ini</sIfile>'
if ($Peripherals -eq 'LPC') {
    Patch-First $prj '<SimDlgDll>DCM.DLL</SimDlgDll>' '<SimDlgDll>DARMP1.DLL</SimDlgDll>'
    Patch-First $prj '<SimDlgDllArguments>-pCM3</SimDlgDllArguments>' '<SimDlgDllArguments>-pLPC1768</SimDlgDllArguments>'
}
Set-Content "$dst\sim.ini" '// placeholder; experiments overwrite this' -Encoding ascii

$cmd = "-b `"$prj`" -t `"Debug`" -j0 -o `"$dst\build.log`""
"UV4.exe $cmd"
$p = Start-Process $uv4 -ArgumentList $cmd -Wait -PassThru -NoNewWindow
"exit=$($p.ExitCode) peripherals=$Peripherals"
Get-Content "$dst\build.log" | Select-Object -Last 3
Get-Item "$dst\Debug\Blinky.axf" | ForEach-Object { "$($_.FullName) $($_.Length) bytes" }
exit $p.ExitCode
