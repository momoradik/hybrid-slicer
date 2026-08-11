# HybridSlicer — Publish Release
# Builds the installer and creates a GitHub release so the auto-updater can pick it up.
#
# Prerequisites:
#   - GitHub CLI (gh) installed and authenticated: https://cli.github.com
#   - Inno Setup 6 installed at default path
#   - Code already committed and pushed to the current branch
#
# Usage:
#   .\publish-release.ps1              # auto-reads version from installer\HybridSlicer.iss
#   .\publish-release.ps1 -Version 1.3.0   # override version

param(
    [string]$Version = ""
)

$ErrorActionPreference = "Stop"

$Root       = Split-Path -Parent $MyInvocation.MyCommand.Path
$Web        = "$Root\web"
$PublishDir = "$Root\publish"
$DistDir    = "$Root\dist"
$Iscc       = @(
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
$IssFile    = "$Root\installer\HybridSlicer.iss"

# ── 0. Read version ────────────────────────────────────────────────────────
if (-not $Version) {
    $match = Select-String -Path $IssFile -Pattern '#define AppVersion\s+"([^"]+)"'
    if ($match) {
        $Version = $match.Matches[0].Groups[1].Value
    } else {
        Write-Host "ERROR: Could not read version from $IssFile" -ForegroundColor Red
        exit 1
    }
}

$Tag = "v$Version"
$InstallerName = "HybridSlicer-Setup.exe"
$InstallerPath = "$DistDir\$InstallerName"

Write-Host ""
Write-Host "================================================" -ForegroundColor Cyan
Write-Host "  HybridSlicer — Publish Release" -ForegroundColor Cyan
Write-Host "  Version : $Version ($Tag)" -ForegroundColor Yellow
Write-Host "================================================" -ForegroundColor Cyan
Write-Host ""

# ── 1. Preflight checks ───────────────────────────────────────────────────
Write-Host "[1/7] Preflight checks..." -ForegroundColor Cyan

if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
    Write-Host "ERROR: GitHub CLI (gh) not found. Install from https://cli.github.com" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $Iscc)) {
    Write-Host "ERROR: Inno Setup 6 not found at $Iscc" -ForegroundColor Red
    exit 1
}

# Check for uncommitted changes
$status = git status --porcelain 2>&1
if ($status) {
    Write-Host "ERROR: Uncommitted changes detected. Commit and push first." -ForegroundColor Red
    git status --short
    exit 1
}

Write-Host "      OK" -ForegroundColor Green

# ── 2. Sync versions across project files ──────────────────────────────────
Write-Host ""
Write-Host "[2/7] Syncing version $Version..." -ForegroundColor Cyan

# Update Launcher .csproj version
$csproj = "$Root\src\HybridSlicer.Launcher\HybridSlicer.Launcher.csproj"
(Get-Content $csproj) -replace '<Version>[^<]+</Version>', "<Version>$Version</Version>" |
    Set-Content $csproj
Write-Host "      Launcher.csproj -> $Version" -ForegroundColor Green

# Update installer .iss version
(Get-Content $IssFile) -replace '#define AppVersion\s+"[^"]+"', "#define AppVersion `"$Version`"" |
    Set-Content $IssFile
Write-Host "      HybridSlicer.iss -> $Version" -ForegroundColor Green

# ── 3. Build frontend ─────────────────────────────────────────────────────
Write-Host ""
Write-Host "[3/7] Building frontend..." -ForegroundColor Cyan
Push-Location $Web
try {
    $npmCmd = if (Get-Command npm -ErrorAction SilentlyContinue) { "npm" }
              elseif (Test-Path "C:\Program Files\nodejs\npm.cmd") { "C:\Program Files\nodejs\npm.cmd" }
              else { throw "npm not found" }
    & $npmCmd run build
    if ($LASTEXITCODE -ne 0) { throw "npm build failed (exit $LASTEXITCODE)" }
} finally { Pop-Location }
Write-Host "      OK" -ForegroundColor Green

# ── 4. Publish self-contained build ────────────────────────────────────────
# Self-contained bundles .NET runtime — users never need to install .NET.
Write-Host ""
Write-Host "[4/7] Publishing self-contained build..." -ForegroundColor Cyan

$PublishOut = "$PublishDir\app"

dotnet publish "$Root\src\HybridSlicer.Api\HybridSlicer.Api.csproj" `
    -c Release --self-contained true -r win-x64 -p:PublishSingleFile=false -p:PublishTrimmed=false -o "$PublishOut" -v q
if ($LASTEXITCODE -ne 0) { throw "API publish failed" }

dotnet publish "$Root\src\HybridSlicer.Launcher\HybridSlicer.Launcher.csproj" `
    -c Release --self-contained true -r win-x64 -p:PublishSingleFile=false -p:PublishTrimmed=false -o "$PublishOut" -v q
if ($LASTEXITCODE -ne 0) { throw "Launcher publish failed" }

Write-Host "      OK" -ForegroundColor Green

# ── 5. Compile installer ──────────────────────────────────────────────────
Write-Host ""
Write-Host "[5/7] Compiling installer..." -ForegroundColor Cyan

if (-not (Test-Path $DistDir)) { New-Item -ItemType Directory -Path $DistDir | Out-Null }

& $Iscc $IssFile
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Inno Setup compile failed" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $InstallerPath)) {
    Write-Host "ERROR: Installer not found at $InstallerPath" -ForegroundColor Red
    exit 1
}

$size = [math]::Round((Get-Item $InstallerPath).Length / 1MB, 1)
Write-Host "      OK -> $InstallerPath ($size MB)" -ForegroundColor Green

# ── 6. Commit version bump (if any changes) ───────────────────────────────
Write-Host ""
Write-Host "[6/7] Committing version bump..." -ForegroundColor Cyan

$bump = git status --porcelain 2>&1
if ($bump) {
    git add "$csproj" "$IssFile"
    git commit -m "chore: bump version to $Version"
    git push
    Write-Host "      Pushed version bump" -ForegroundColor Green
} else {
    Write-Host "      No version changes to commit" -ForegroundColor DarkGray
}

# ── 7. Create GitHub release ──────────────────────────────────────────────
Write-Host ""
Write-Host "[7/7] Creating GitHub release $Tag..." -ForegroundColor Cyan

$notes = @"
## HybridSlicer $Version

### Installation
Download **$InstallerName** below and run it. The installer auto-detects x64/x86.

### Auto-Update
Existing installations will detect this release automatically and prompt to update.
"@

gh release create $Tag $InstallerPath `
    --title "HybridSlicer $Version" `
    --notes $notes `
    --latest

if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: gh release create failed" -ForegroundColor Red
    Write-Host "       The tag $Tag may already exist. Delete it first:" -ForegroundColor Yellow
    Write-Host "       gh release delete $Tag --yes && git push --delete origin $Tag" -ForegroundColor Yellow
    exit 1
}

Write-Host ""
Write-Host "================================================" -ForegroundColor Green
Write-Host "  Release $Tag published!" -ForegroundColor Green
Write-Host "  Users will see the update in the launcher." -ForegroundColor Green
Write-Host "================================================" -ForegroundColor Green
Write-Host ""
