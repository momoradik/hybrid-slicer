# HybridSlicer build script
# Steps: stop server, build frontend, build .NET, copy launcher, start via launcher
# Bonus: if Inno Setup 6 is installed, also publishes self-contained + compiles installer

$ErrorActionPreference = "Stop"

$Root        = Split-Path -Parent $MyInvocation.MyCommand.Path
$Web         = "$Root\web"
$ApiOut      = "$Root\src\HybridSlicer.Api\bin\Debug\net8.0"
$LauncherOut = "$Root\src\HybridSlicer.Launcher\bin\Debug\net8.0-windows"
$PublishDir  = "$Root\publish"
$LogDir      = "$Root\logs"
$Iscc        = @(
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1

$LocalIP = (
    Get-NetIPAddress -AddressFamily IPv4 |
    Where-Object { $_.IPAddress -notmatch '^127\.' -and
                   $_.IPAddress -notmatch '^169\.' -and
                   $_.PrefixOrigin -ne 'WellKnown' } |
    Select-Object -First 1
).IPAddress
if (-not $LocalIP) { $LocalIP = "localhost" }

Write-Host ""
Write-Host "================================================" -ForegroundColor Cyan
Write-Host "  HybridSlicer Build Script" -ForegroundColor Cyan
Write-Host "  Network IP : $LocalIP" -ForegroundColor Yellow
Write-Host "================================================" -ForegroundColor Cyan
Write-Host ""

# 1 - Stop existing server and launcher window
Write-Host "[1/5] Stopping existing server and launcher..." -ForegroundColor DarkGray

# Kill the API process listening on port 5000
$conn = Get-NetTCPConnection -LocalPort 5000 -State Listen -ErrorAction SilentlyContinue
if ($conn) {
    Stop-Process -Id $conn[0].OwningProcess -Force -ErrorAction SilentlyContinue
    Write-Host "      Stopped API (PID $($conn[0].OwningProcess))." -ForegroundColor DarkGray
} else {
    Write-Host "      No server on port 5000." -ForegroundColor DarkGray
}

# Kill any open launcher window (holds HybridSlicer.dll locked between runs)
Get-Process -Name "HybridSlicer" -ErrorAction SilentlyContinue | ForEach-Object {
    $_.Kill()
    Write-Host "      Stopped launcher (PID $($_.Id))." -ForegroundColor DarkGray
}

Start-Sleep -Milliseconds 800

# 2 - Build frontend
Write-Host ""
Write-Host "[2/5] Building frontend (npm run build)..." -ForegroundColor Cyan
Push-Location $Web
try {
    npm run build
    if ($LASTEXITCODE -ne 0) { throw "npm build failed (exit $LASTEXITCODE)" }
} finally {
    Pop-Location
}
Write-Host "      Frontend OK." -ForegroundColor Green

# 3 - Build .NET solution (includes Launcher)
Write-Host ""
Write-Host "[3/5] Building .NET solution..." -ForegroundColor Cyan
Push-Location $Root
try {
    dotnet restore -v q
    dotnet build --no-restore -v q
    if ($LASTEXITCODE -ne 0) { throw "dotnet build failed (exit $LASTEXITCODE)" }
} finally {
    Pop-Location
}
Write-Host "      Build OK." -ForegroundColor Green

# 4 - Copy launcher artifacts next to the API binary
Write-Host ""
Write-Host "[4/5] Copying launcher to API output..." -ForegroundColor Cyan
if (Test-Path $LauncherOut) {
    Get-ChildItem "$LauncherOut\HybridSlicer.*" | ForEach-Object {
        Copy-Item $_.FullName -Destination $ApiOut -Force
    }
    Get-ChildItem "$LauncherOut\Microsoft.Web.WebView2.*.dll" -ErrorAction SilentlyContinue | ForEach-Object {
        Copy-Item $_.FullName -Destination $ApiOut -Force
    }
    # Copy WebView2 native runtime
    $wv2Runtime = "$LauncherOut\runtimes"
    if (Test-Path $wv2Runtime) {
        Copy-Item $wv2Runtime -Destination $ApiOut -Recurse -Force
    }
    Write-Host "      OK -> $ApiOut\HybridSlicer.exe" -ForegroundColor Green
} else {
    Write-Host "      WARNING: $LauncherOut not found" -ForegroundColor Yellow
}

# 5 - Start API directly (dev mode; end users double-click HybridSlicer.exe which
#     runs first-time setup before starting the server)
Write-Host ""
Write-Host "[5/5] Starting server..." -ForegroundColor Cyan

if (-not (Test-Path $LogDir)) { New-Item -ItemType Directory -Path $LogDir | Out-Null }

Start-Process dotnet `
    -ArgumentList "HybridSlicer.Api.dll --urls `"http://*:5000`"" `
    -WorkingDirectory $ApiOut `
    -WindowStyle Hidden `
    -RedirectStandardOutput "$LogDir\server.log" `
    -RedirectStandardError  "$LogDir\server-err.log"

$started = $false
for ($i = 0; $i -lt 20; $i++) {
    Start-Sleep -Seconds 1
    $c = Get-NetTCPConnection -LocalPort 5000 -State Listen -ErrorAction SilentlyContinue
    if ($c) { $started = $true; break }
    Write-Host "      Waiting... ($($i+1)s)" -ForegroundColor DarkGray
}

Write-Host ""
if ($started) {
    Write-Host "================================================" -ForegroundColor Green
    Write-Host "  Server is running!" -ForegroundColor Green
    Write-Host ""
    Write-Host "  Local   : http://localhost:5000"  -ForegroundColor White
    Write-Host "  Network : http://${LocalIP}:5000" -ForegroundColor Yellow
    Write-Host "  API docs: http://localhost:5000/swagger" -ForegroundColor DarkGray
    Write-Host "================================================" -ForegroundColor Green
} else {
    Write-Host "================================================" -ForegroundColor Red
    Write-Host "  Server did NOT start within 20 seconds." -ForegroundColor Red
    Write-Host "  Check logs: $LogDir" -ForegroundColor Red
    Write-Host "================================================" -ForegroundColor Red
    if (Test-Path "$LogDir\server-err.log") {
        Get-Content "$LogDir\server-err.log" | Select-Object -Last 20
    }
    exit 1
}

# Optional: publish self-contained + compile Inno Setup installer
Write-Host ""
Write-Host "-- Installer (optional) ---" -ForegroundColor DarkGray

if (Test-Path $Iscc) {
    Write-Host "   Inno Setup found. Publishing self-contained builds..." -ForegroundColor DarkGray

    $PublishX64 = "$PublishDir\x64"
    $PublishX86 = "$PublishDir\x86"

    # x64 build
    Write-Host "   Publishing x64..." -ForegroundColor DarkGray
    dotnet publish "$Root\src\HybridSlicer.Api\HybridSlicer.Api.csproj" `
        -c Release -r win-x64 --self-contained true `
        -p:PublishSingleFile=false `
        -o "$PublishX64" -v q
    if ($LASTEXITCODE -ne 0) { Write-Host "   API x64 publish failed." -ForegroundColor Yellow }
    dotnet publish "$Root\src\HybridSlicer.Launcher\HybridSlicer.Launcher.csproj" `
        -c Release -r win-x64 --self-contained true `
        -p:PublishSingleFile=false `
        -o "$PublishX64" -v q

    # x86 build
    Write-Host "   Publishing x86..." -ForegroundColor DarkGray
    dotnet publish "$Root\src\HybridSlicer.Api\HybridSlicer.Api.csproj" `
        -c Release -r win-x86 --self-contained true `
        -p:PublishSingleFile=false `
        -o "$PublishX86" -v q
    if ($LASTEXITCODE -ne 0) { Write-Host "   API x86 publish failed." -ForegroundColor Yellow }
    dotnet publish "$Root\src\HybridSlicer.Launcher\HybridSlicer.Launcher.csproj" `
        -c Release -r win-x86 --self-contained true `
        -p:PublishSingleFile=false `
        -o "$PublishX86" -v q

    if ($LASTEXITCODE -eq 0) {
        Write-Host "   Compiling installer..." -ForegroundColor DarkGray
        & $Iscc "$Root\installer\HybridSlicer.iss"
        if ($LASTEXITCODE -eq 0) {
            Write-Host "   Installer -> dist\HybridSlicer-Setup.exe" -ForegroundColor Green
        } else {
            Write-Host "   Inno Setup compile failed." -ForegroundColor Yellow
        }
    }
} else {
    Write-Host "   Inno Setup 6 not found - skipping installer build." -ForegroundColor DarkGray
    Write-Host "   Install Inno Setup 6 to enable installer generation." -ForegroundColor DarkGray
}
