# PowerShell pre-build actions for mEEM-demo project
Write-Host "Starting mEEM pre-build actions..." -ForegroundColor Blue

# Navigate to project root
Set-Location "$PSScriptRoot\..\..\.."

# Step 1: Check for uv
Write-Host "Step 1: Checking for uv..." -ForegroundColor Yellow
if (-not (Get-Command uv -ErrorAction SilentlyContinue)) {
    Write-Host "ERROR: uv was not found in PATH. Install it from https://docs.astral.sh/uv/getting-started/installation/" -ForegroundColor Red
    exit 1
}

# Step 2: Generate MEEM configuration
Write-Host "Step 2: Generating MEEM configuration..." -ForegroundColor Yellow

$configGen = ".\tools\meem_config_gen\meem_config_gen.py"
$dataModel = ".\example\Microchip\MEEM_Config\eeprom_datamodel_example.json"
$platformSettings = ".\example\Microchip\MEEM_Config\platform_settings_xc8.json"
$outputDir = ".\example\Microchip\MEEM_Config\generated"

# Create a directory for generated MEEM configuration
New-Item -Path (Split-Path -Path $outputDir -Parent) -Name "generated" -ItemType "Directory" -Force

# `uv run` provisions the Python interpreter and the dependencies on demand
& uv run $configGen $dataModel $platformSettings $outputDir
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Failed to generate MEEM configuration" -ForegroundColor Red
    exit 1
}

Write-Host "MEEM configuration generated successfully" -ForegroundColor Green
Write-Host "mEEM pre-build actions completed successfully!" -ForegroundColor Green
exit 0
