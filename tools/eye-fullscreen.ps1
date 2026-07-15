#requires -version 5
<#
  Oko maga - launch fullscreen, like a game.

  Maximizes the CURRENT console window (a Windows action), then builds and runs
  an example through Docker (gcc:13) in colour. The Linux binary inside the
  container then sees the already-maximized window width and centres the panel.

  Why a separate script: from inside a Docker (Linux) container the library
  cannot resize the Windows console - the Win32 maximize in render.hpp only
  applies to a NATIVE Windows build. This launcher closes that gap for the
  Docker workflow.

  Usage (from the repo root):
      powershell -ExecutionPolicy Bypass -File tools\eye-fullscreen.ps1
      powershell -ExecutionPolicy Bypass -File tools\eye-fullscreen.ps1 05_single_inheritance
#>
param([string]$Example = "06_polymorphism")

# --- 1. maximize the current console window ---------------------------------
$sig = '[System.Runtime.InteropServices.DllImport("kernel32.dll")] public static extern System.IntPtr GetConsoleWindow(); [System.Runtime.InteropServices.DllImport("user32.dll")] public static extern bool ShowWindow(System.IntPtr hWnd, int nCmdShow);'
Add-Type -Namespace EyeNative -Name Win -MemberDefinition $sig
$SW_MAXIMIZE = 3
$hwnd = [EyeNative.Win]::GetConsoleWindow()
if ($hwnd -ne [System.IntPtr]::Zero) {
    [void][EyeNative.Win]::ShowWindow($hwnd, $SW_MAXIMIZE)
    Start-Sleep -Milliseconds 250
} else {
    Write-Host "Console window not found - maximize it manually (F11) and rerun." -ForegroundColor Yellow
}

# --- 2. build and run the example through Docker ----------------------------
$repo    = Split-Path -Parent $PSScriptRoot
$repoFwd = $repo -replace '\\', '/'
$cmd     = "g++ -std=c++20 -Iinclude examples/$Example.cpp -o /tmp/e && /tmp/e"

Write-Host "Oko maga - $Example (fullscreen)" -ForegroundColor Cyan
docker run --rm -it -e EYE_COLOR=1 -v "${repoFwd}:/src:ro" -w /src gcc:13 bash -c $cmd
