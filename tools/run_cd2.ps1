# Launch the Crackdown 2 recomp on Windows.
#   .\tools\run_cd2.ps1 [-Config relwithdebinfo|debug|release] [-Assets <path>] [-Update <path>] [-Backend any|d3d12|vulkan]
#                       [-Cheats] [-Readback] [-PageRefresh] [-VSync] [extra args...]
# -Cheats   enables the hidden cheat rows at the end of Options > Display (--c2_cheats=true).
# -Readback keeps the runtime's GPU memexport readback on (default off: 3-10x faster in game, see README "Performance").
# -PageRefresh keeps the runtime's per-frame GPU page-state refresh on (default off: the game re-uploads ~20 MB of
#           unchanged vertex data every frame with it on, see README "Performance").
# -VSync    keeps v-sync on (default off so the frame rate is not capped at the display's refresh / 2).
param(
    [string]$Config = "relwithdebinfo",
    [string]$Assets = "",
    [string]$Update = "",
    [string]$Backend = "any",
    [switch]$Cheats,
    [switch]$Readback,
    [switch]$PageRefresh,
    [switch]$VSync,
    [Parameter(ValueFromRemainingArguments = $true)] [string[]]$Rest
)
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if ($Assets -eq "") { $Assets = Join-Path $root "assets" }
$build = Join-Path $root "out\build\win-amd64-$Config"
$exe = Join-Path $build "crackdown2.exe"
if (-not (Test-Path $exe)) { Write-Error "Executable not found: $exe (build the win-amd64-$Config preset first)"; exit 1 }
$args = @("--game_data_root=$Assets", "--gpu_plugin=xenos", "--c2_gpu_backend=$Backend", "--c2_fps_display")
if ($Update -ne "") { $args += "--update_data_root=$Update" }
if (-not $Readback) { $args += "--readback_memexport=false" }
if (-not $PageRefresh) { $args += "--clear_memory_page_state=false" }
if (-not $VSync) { $args += "--vsync=false" }
if ($Cheats) { $args += "--c2_cheats=true" }
$args += $Rest
Write-Host "Running: $exe $($args -join ' ')"
& $exe @args
