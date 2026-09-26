# Snapshots a uVision firmware project into reference/<Name> and builds it.
# The source folder is only read. The copy's only change: sIfile=.\sim.ini, so
# run-sim.ps1 experiments can drive it. Both reference projects already select the
# simulator with DARMP1.DLL -pLPC1768, so nothing else is patched.
#   snapshot-project.ps1 -Source <bit-band test project folder> -Project Bitband -Name bitband-test
#   snapshot-project.ps1 -Source <joystick/LED demo project folder> -Project Blinky -Name joystick-led-demo
param([Parameter(Mandatory)][string]$Source, [Parameter(Mandatory)][string]$Project, [Parameter(Mandatory)][string]$Name)
$ErrorActionPreference = 'Stop'
$src = $Source
$dst = Join-Path (Split-Path $PSScriptRoot) "reference\$Name"

if (Test-Path $dst) { Remove-Item $dst -Recurse -Force }
New-Item -ItemType Directory $dst | Out-Null
Get-ChildItem $src -Force | Where-Object { $_.Name -notin 'Objects', 'Listings' -and $_.Name -notlike '*.uvguix*' } |
    Copy-Item -Destination $dst -Recurse
Get-ChildItem $dst -Recurse -File | ForEach-Object { $_.IsReadOnly = $false }

$opt = "$dst\$Project.uvoptx"
$text = [IO.File]::ReadAllText($opt)
if ($text -notmatch '<sIfile></sIfile>') { throw 'sIfile pattern not found' }
[IO.File]::WriteAllText($opt, ([regex]'<sIfile></sIfile>').Replace($text, '<sIfile>.\sim.ini</sIfile>', 1))
Set-Content "$dst\sim.ini" '// placeholder; experiments overwrite this' -Encoding ascii
"snapshot of $src taken $(Get-Date -Format s)" | Set-Content "$dst\SNAPSHOT.txt"

$p = Start-Process 'C:\Keil_v5\UV4\UV4.exe' -ArgumentList "-r `"$dst\$Project.uvprojx`" -j0 -o `"$dst\build.log`"" -Wait -PassThru -NoNewWindow
"exit=$($p.ExitCode)"
Get-Content "$dst\build.log" | Select-Object -Last 3
exit $p.ExitCode
