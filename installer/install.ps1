```powershell
$ErrorActionPreference = "Stop"

$Owner = "filyx0"
$Repository = "FlowTools"
$InstallRoot = "C:\FlowTools"
$BinRoot = Join-Path $InstallRoot "bin"

Write-Host ""
Write-Host "FlowTools Installer"
Write-Host "=================="
Write-Host ""

Write-Host "-> Checking latest release..."

$Headers = @{
    "Accept" = "application/vnd.github+json"
    "User-Agent" = "FlowTools-Installer"
}

$ReleaseUrl = "https://api.github.com/repos/$Owner/$Repository/releases/latest"

try {
    $Release = Invoke-RestMethod `
        -Uri $ReleaseUrl `
        -Headers $Headers `
        -Method Get
}
catch {
    Write-Host ""
    Write-Host "[ERROR] Unable to retrieve the latest FlowTools release." -ForegroundColor Red
    Write-Host $_.Exception.Message
    exit 1
}

Write-Host "[OK] Release $($Release.tag_name) found" -ForegroundColor Green

$Assets = @{}

foreach ($Asset in $Release.assets) {
    $Assets[$Asset.name] = $Asset
}

$RequiredAssets = @(
    "flow.exe",
    "core.dll",
    "data.json"
)

foreach ($Required in $RequiredAssets) {
    if (-not $Assets.ContainsKey($Required)) {
        Write-Host ""
        Write-Host "[ERROR] Release asset not found: $Required" -ForegroundColor Red
        exit 1
    }
}

Write-Host "-> Creating installation directory..."

New-Item `
    -ItemType Directory `
    -Force `
    -Path $BinRoot | Out-Null

Write-Host "[OK] $InstallRoot" -ForegroundColor Green
Write-Host ""

foreach ($Name in $RequiredAssets) {
    $Asset = $Assets[$Name]

    if ($Name -eq "data.json") {
        $Destination = Join-Path $InstallRoot $Name
    }
    else {
        $Destination = Join-Path $BinRoot $Name
    }

    Write-Host "-> Downloading $Name"

    try {
        Invoke-WebRequest `
            -Uri $Asset.browser_download_url `
            -OutFile $Destination `
            -UseBasicParsing
    }
    catch {
        Write-Host ""
        Write-Host "[ERROR] Failed to download $Name" -ForegroundColor Red
        Write-Host $_.Exception.Message
        exit 1
    }

    if (-not (Test-Path $Destination)) {
        Write-Host ""
        Write-Host "[ERROR] Downloaded file does not exist: $Destination" -ForegroundColor Red
        exit 1
    }

    $ActualSize = (Get-Item $Destination).Length

    if ($Asset.size -gt 0 -and $ActualSize -ne $Asset.size) {
        Write-Host ""
        Write-Host "[ERROR] File size verification failed for $Name" -ForegroundColor Red
        Remove-Item -Force $Destination -ErrorAction SilentlyContinue
        exit 1
    }

    if ($Asset.digest -and $Asset.digest.StartsWith("sha256:")) {
        $ExpectedHash = $Asset.digest.Substring(7).ToLowerInvariant()
        $ActualHash = (Get-FileHash -Path $Destination -Algorithm SHA256).Hash.ToLowerInvariant()

        if ($ActualHash -ne $ExpectedHash) {
            Write-Host ""
            Write-Host "[ERROR] SHA-256 verification failed for $Name" -ForegroundColor Red
            Remove-Item -Force $Destination -ErrorAction SilentlyContinue
            exit 1
        }

        Write-Host "  SHA-256 verified" -ForegroundColor Green
    }

    Write-Host "  [OK] $Name" -ForegroundColor Green
}

Write-Host ""
Write-Host "-> Configuring PATH..."

$UserPath = [Environment]::GetEnvironmentVariable(
    "Path",
    "User"
)

$PathEntries = @()

if ($UserPath) {
    $PathEntries = $UserPath -split ";"
}

$NormalizedBinRoot = $BinRoot.TrimEnd("\").ToLowerInvariant()

$AlreadyInPath = $false

foreach ($Entry in $PathEntries) {
    if ($Entry.Trim().TrimEnd("\").ToLowerInvariant() -eq $NormalizedBinRoot) {
        $AlreadyInPath = $true
        break
    }
}

if (-not $AlreadyInPath) {
    $NewUserPath = if ($UserPath) {
        "$UserPath;$BinRoot"
    }
    else {
        $BinRoot
    }

    [Environment]::SetEnvironmentVariable(
        "Path",
        $NewUserPath,
        "User"
    )

    Write-Host "[OK] Added $BinRoot to user PATH" -ForegroundColor Green
}
else {
    Write-Host "[OK] PATH already configured" -ForegroundColor Green
}

$env:Path = "$BinRoot;$env:Path"

Write-Host ""
Write-Host "=========================================="
Write-Host "FlowTools installed successfully!"
Write-Host "Version: $($Release.tag_name)"
Write-Host "Location: $InstallRoot"
Write-Host "=========================================="
Write-Host ""

Write-Host "-> Starting Flow..."

$FlowExecutable = Join-Path $BinRoot "flow.exe"

if (-not (Test-Path $FlowExecutable)) {
    Write-Host "[ERROR] flow.exe was not installed." -ForegroundColor Red
    exit 1
}

Start-Process `
    -FilePath $FlowExecutable `
    -WorkingDirectory $InstallRoot
```
