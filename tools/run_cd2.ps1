# Launch the Crackdown 2 recomp on Windows.
#   .\tools\run_cd2.ps1 [-Config relwithdebinfo|debug|release] [-Assets <path>] [-Update <path>] [-Backend any|d3d12|vulkan] [extra args...]
param(
    [string]$Config = "relwithdebinfo",
    [string]$Assets = "",
    [string]$Update = "",
    [string]$Backend = "any",
    [Parameter(ValueFromRemainingArguments = $true)] [string[]]$Rest
)
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if ($Assets -eq "") { $Assets = Join-Path $root "assets" }
$build = Join-Path $root "out\build\win-amd64-$Config"
$exe = Join-Path $build "crackdown2.exe"
if (-not (Test-Path $exe)) { Write-Error "Executable not found: $exe (build the win-amd64-$Config preset first)"; exit 1 }
$args = @("--game_data_root=$Assets", "--gpu_plugin=xenos", "--c2_gpu_backend=$Backend", "--c2_fps_display")
if ($Update -ne "") { $args += "--update_data_root=$Update" }
$args += $Rest
Write-Host "Running: $exe $($args -join ' ')"
& $exe @args
