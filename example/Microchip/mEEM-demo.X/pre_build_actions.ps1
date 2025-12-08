# PowerShell pre-build actions for mEEM-demo project
Write-Host "Starting mEEM pre-build actions..." -ForegroundColor Blue

# Navigate to project root
Set-Location "$PSScriptRoot\..\..\.."

# Step 1: Setup Python virtual environment
Write-Host "Step 1: Setting up Python virtual environment..." -ForegroundColor Yellow
try {
    & ".\setup_venv.ps1"
    if ($LASTEXITCODE -ne 0) {
        throw "Virtual environment setup failed"
    }
}
catch {
    Write-Host "ERROR: $_" -ForegroundColor Red
    exit 1
}

# Step 3: Generate MEEM configuration
Write-Host "Step 2: Generating MEEM configuration..." -ForegroundColor Yellow

$pythonCmd = ".\.venv_meem\Scripts\python.exe"
$configGen = ".\tools\meem_config_gen\meem_config_gen.py"
$dataModel = ".\example\Microchip\MEEM_Config\eeprom_datamodel_example.json"
$platformSettings = ".\example\Microchip\MEEM_Config\platform_settings_xc8.json"
$outputDir = ".\example\Microchip\MEEM_Config\generated"

# Create a directory for generated MEEM configuration
New-Item -Path (Split-Path -Path $outputDir -Parent) -Name "generated" -ItemType "Directory" -Force

& $pythonCmd $configGen $dataModel $platformSettings $outputDir
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Failed to generate MEEM configuration" -ForegroundColor Red
    exit 1
}

Write-Host "MEEM configuration generated successfully" -ForegroundColor Green
Write-Host "mEEM pre-build actions completed successfully!" -ForegroundColor Green
exit 0