<#
.SYNOPSIS
    Install Merak on Windows — downloads the latest release and sets up shortcuts.
.DESCRIPTION
    Run: irm https://raw.merak.dev/install.ps1 | iex
#>
param (
    [string]$InstallDir = "$env:USERPROFILE\merak"
)

$ErrorActionPreference = "Stop"
$Repo = "ULookup/Merak"

Write-Host "=== Merak Installer ===" -ForegroundColor Green
Write-Host "Install to: $InstallDir"

# 1. Get latest release
Write-Host "[1/5] Fetching latest release..." -ForegroundColor Cyan
try {
    $release = Invoke-RestMethod -Uri "https://api.github.com/repos/$Repo/releases/latest"
    $tag = $release.tag_name
    Write-Host "  Latest: $tag"
} catch {
    Write-Host "  Cannot fetch release info: $_" -ForegroundColor Red
    exit 1
}

# 2. Download
$url = "https://github.com/$Repo/releases/download/$tag/merak-windows-x64.zip"
$zipPath = "$env:TEMP\merak-$tag.zip"
Write-Host "[2/5] Downloading $url ..." -ForegroundColor Cyan
try {
    Invoke-WebRequest -Uri $url -OutFile $zipPath
} catch {
    Write-Host "  Download failed: $_" -ForegroundColor Red
    exit 1
}

# 3. Extract
Write-Host "[3/5] Extracting..." -ForegroundColor Cyan
if (Test-Path $InstallDir) {
    Remove-Item -Recurse -Force $InstallDir
}
Expand-Archive -Path $zipPath -DestinationPath $InstallDir
Remove-Item $zipPath

# 4. Create desktop shortcut
Write-Host "[4/5] Creating shortcut..." -ForegroundColor Cyan
$WshShell = New-Object -ComObject WScript.Shell
$Shortcut = $WshShell.CreateShortcut("$env:USERPROFILE\Desktop\Merak.lnk")
$Shortcut.TargetPath = "$InstallDir\merak-launcher.exe"
$Shortcut.IconLocation = "$InstallDir\merak-launcher.exe,0"
$Shortcut.Save()

# 5. Init config if not exists
Write-Host "[5/5] Setting up config..." -ForegroundColor Cyan
$merakHome = "$env:USERPROFILE\.merak"
if (-not (Test-Path "$merakHome\settings.local.json")) {
    New-Item -ItemType Directory -Force -Path $merakHome | Out-Null
    $template = @'
{
  "llm": {"provider":"anthropic","api_key":"","default_model":"claude-sonnet-4-6","max_output_tokens":4096},
  "agent": {"system_prompt":"You are a creative writing AI assistant. Use tools to help build worlds and stories."},
  "memory": {"enabled":false}
}
'@
    Set-Content -Path "$merakHome\settings.local.json" -Value $template
    Write-Host "  Config created at $merakHome\settings.local.json" -ForegroundColor Yellow
    Write-Host "  Configure your API key in the WebUI Settings page." -ForegroundColor Yellow
}

Write-Host "=== Done! ===" -ForegroundColor Green
Write-Host "Double-click the Merak icon on your desktop to start."
