# Launch the Crackdown 2 recomp on Windows.
#   .\tools\run_cd2.ps1 [-Config relwithdebinfo|debug|release] [-Assets <path>] [-Update <path>] [-Backend any|d3d12|vulkan]
#   (-Assets/-Update default to the paths tools\setup_game.py wrote to cd2_game.json)
#                       [-Cheats] [-Readback] [-PageRefresh] [-VSync] [-NoKeyboard] [extra args...]
# -Cheats   enables the hidden cheat rows at the end of Options > Display (--c2_cheats=true).
# -Readback keeps the runtime's GPU memexport readback on (default off: 3-10x faster in game, see README "Performance").
# -PageRefresh keeps the runtime's per-frame GPU page-state refresh on (default off: the game re-uploads ~20 MB of
#           unchanged vertex data every frame with it on, see README "Performance").
# -NoKeyboard leaves keyboard/mouse gamepad emulation off (default on: --mnk_mode --mnk_mouse=true, so the
#           "press START" screen and menus work without a controller; a real controller still works alongside).
# -VSync    keeps v-sync on (default off so the frame rate is not capped at the display's refresh / 2).
[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$Config = "relwithdebinfo",
    [string]$Assets = "",
    [string]$Update = "",
    [string]$Backend = "any",
    [switch]$Cheats,
    [switch]$Readback,
    [switch]$PageRefresh,
    [switch]$VSync,
    [switch]$NoKeyboard,
    [Parameter(ValueFromRemainingArguments = $true)] [string[]]$Rest
)
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
# tools\setup_game.py records the extracted disc and title update paths here.
$cfgPath = Join-Path $root "cd2_game.json"
if (Test-Path $cfgPath) {
    $cfg = Get-Content $cfgPath -Raw | ConvertFrom-Json
    if ($Assets -eq "" -and $cfg.assets) { $Assets = $cfg.assets }
    if ($Update -eq "" -and $cfg.update) { $Update = $cfg.update }
}
if ($Assets -eq "") { $Assets = Join-Path $root "assets" }
if (-not (Test-Path (Join-Path $Assets "default.xex"))) {
    Write-Error "No default.xex in $Assets. Run: python tools\setup_game.py --iso <disc.iso> --tu <title update> [--dlc <packages>]"; exit 1
}
$build = Join-Path $root "out\build\win-amd64-$Config"
$exe = Join-Path $build "crackdown2.exe"
# Release zips ship crackdown2.exe next to this script's parent folder instead of under out\build.
if (-not (Test-Path $exe) -and (Test-Path (Join-Path $root "crackdown2.exe"))) { $exe = Join-Path $root "crackdown2.exe" }
if (-not (Test-Path $exe)) { Write-Error "Executable not found: $exe (build the win-amd64-$Config preset first, or run from an extracted release)"; exit 1 }
$args = @("--game_data_root=$Assets", "--gpu_plugin=xenos", "--c2_gpu_backend=$Backend")
if ($Update -ne "") { $args += "--update_data_root=$Update" }
if (-not $Readback) { $args += "--readback_memexport=false" }
if (-not $PageRefresh) { $args += "--clear_memory_page_state=false" }
if (-not $VSync) { $args += "--vsync=false" }
if (-not $NoKeyboard) { $args += "--mnk_mode", "--mnk_mouse=true" }
if ($Cheats) { $args += "--c2_cheats=true" }
$args += $Rest
Write-Host "Running: $exe $($args -join ' ')"
& $exe @args
