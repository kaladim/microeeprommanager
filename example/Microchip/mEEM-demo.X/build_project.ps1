# PowerShell script to build mEEM-demo project using MPLAB X command line tools

Write-Host "mEEM-demo Command Line Build Script" -ForegroundColor Blue
Write-Host "====================================" -ForegroundColor Blue

# Build using MPLAB X command line tools
Write-Host "`nBuilding project using MPLAB X tools..." -ForegroundColor Yellow

# Check if MPLAB X path is already cached in environment variable
$mplabxPath = $env:MPLABX_PATH
if (-not $mplabxPath -or -not (Test-Path $mplabxPath)) {
    Write-Host "Discovering MPLAB X installation..." -ForegroundColor Cyan
    
    $mplabxPaths = @(
        "C:\Program Files\Microchip\MPLABX\*",
        "C:\Program Files (x86)\Microchip\MPLABX\*"
    )

    foreach ($pathPattern in $mplabxPaths) {
        $foundPath = Get-ChildItem $pathPattern -Directory -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($foundPath) {
            $mplabxPath = $foundPath.FullName
            Write-Host "Found MPLAB X at: $mplabxPath" -ForegroundColor Cyan
            
            # Cache the path for future runs
            $env:MPLABX_PATH = $mplabxPath
            Write-Host "Cached path in MPLABX_PATH environment variable" -ForegroundColor Gray
            break
        }
    }
}
else {
    Write-Host "Using cached MPLAB X path: $mplabxPath" -ForegroundColor Cyan
}

if ($mplabxPath) {
    $prjGenerator = Join-Path $mplabxPath "mplab_platform\bin\prjMakefilesGenerator.bat"
    $gnuMake = Join-Path $mplabxPath "gnuBins\GnuWin32\bin\make.exe"
    
    if (Test-Path $prjGenerator) {
        # Only regenerate makefiles if they don't exist or -Force parameter is used
        if (-not (Test-Path "Makefile")) {
            Write-Host "Regenerating makefiles..." -ForegroundColor Cyan
            & $prjGenerator -v (Get-Location).Path
        }
        else {
            Write-Host "Makefiles already exist, skipping regeneration" -ForegroundColor Gray
        }
        
        if ((Test-Path "Makefile") -and (Test-Path $gnuMake)) {
            Write-Host "Building with MPLAB X make..." -ForegroundColor Cyan
            & $gnuMake build
            if ($LASTEXITCODE -eq 0) {
                Write-Host "`n========================================" -ForegroundColor Green
                Write-Host "BUILD COMPLETED SUCCESSFULLY!" -ForegroundColor Green  
                Write-Host "========================================" -ForegroundColor Green
                Write-Host "`nOutput files are in the 'dist' directory" -ForegroundColor White
                exit 0
            }
        }
    }
}

# Build failed
Write-Host "`n========================================" -ForegroundColor Red
Write-Host "BUILD FAILED OR NOT COMPLETED" -ForegroundColor Red
Write-Host "========================================" -ForegroundColor Red

Write-Host "`nSolution:" -ForegroundColor Yellow
Write-Host "Install MPLAB X IDE from: https://www.microchip.com/mplab/mplab-x-ide" -ForegroundColor White

exit 1