# Runs one simulator experiment: installs an INI as <project dir>\sim.ini (the
# project's sIfile), starts µVision in debug mode (-d, hidden with -j0), waits up to
# -TimeoutSec for UV4 to exit by itself, and reports whether it had to be killed.
# The INI writes its own results with LOG >, so the log is the result.
param(
    [Parameter(Mandatory)][string]$Ini,
    [string]$Project = '',   # default below: $PSScriptRoot is empty in param defaults on PS 5.1
    [string]$Target = 'Debug',
    [int]$TimeoutSec = 60,
    [switch]$Visible
)
$ErrorActionPreference = 'Stop'
if (-not $Project) { $Project = Join-Path (Split-Path $PSScriptRoot) 'reference\blinky\Blinky.uvprojx' }
$dst = Split-Path $Project
$log = Join-Path $env:TEMP 'latasim-sim.log'   # LOG > rejects paths with spaces (error 10)

# µVision saves a session's breakpoints into the project's .uvoptx on exit and restores
# them after the INI runs (so an INI "BK *" can't clear them). Start every run clean,
# using Keil's own empty form.
$opt = [IO.Path]::ChangeExtension($Project, '.uvoptx')
[IO.File]::WriteAllText($opt, ([regex]::Replace([IO.File]::ReadAllText($opt), '<Breakpoint>[\s\S]*?</Breakpoint>', '<Breakpoint/>')))

Remove-Item $log -EA SilentlyContinue
(Get-Content $Ini -Raw).Replace('%LOG%', $log) | Set-Content "$dst\sim.ini" -Encoding ascii

$uvArgs = "-d `"$Project`" -t `"$Target`"" + $(if ($Visible) { '' } else { ' -j0' })
$before = @(Get-Process UV4 -EA SilentlyContinue | ForEach-Object Id)   # e.g. an interactive µVision; never touched
$sw = [Diagnostics.Stopwatch]::StartNew()
$p = Start-Process 'C:\Keil_v5\UV4\UV4.exe' -ArgumentList $uvArgs -PassThru
$exited = $p.WaitForExit($TimeoutSec * 1000)
if (-not $exited) { Stop-Process -Id $p.Id -Force; $p.WaitForExit() }
"uv4 exited_by_itself=$exited exit=$($p.ExitCode) wall=$([int]$sw.Elapsed.TotalSeconds)s"
$orphans = Get-Process UV4 -EA SilentlyContinue | Where-Object { $before -notcontains $_.Id }
"orphan_uv4=$(@($orphans).Count)"
if (Test-Path $log) { Copy-Item $log "$dst\sim.log"; Get-Content $log } else { '(no sim.log written)' }
