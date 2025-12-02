try {
    if (!(Test-Path -Path ".\.venv_meem")) {
        Write-Host "Virtual environment not found, creating one..." -ForegroundColor Yellow
        
        # Create virtual environment
        py -m venv .venv_meem
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to create virtual environment"
        }
        
        # Verify virtual environment was created properly
        if (!(Test-Path ".\.venv_meem\Scripts\python.exe")) {
            throw "Virtual environment creation incomplete"
        }
        
        # Upgrade pip to avoid warning during installation of the requirements
        & ".\.venv_meem\Scripts\python.exe" -m pip install --upgrade pip
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to upgrade pip"
        }
        
        # Install requirements
        & ".\.venv_meem\Scripts\python.exe" -m pip install -r .\requirements.txt
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to install requirements"
        }
        
        Write-Host "Virtual environment created successfully" -ForegroundColor Green
    }
    else {
        Write-Host "Virtual environment found, verifying..." -ForegroundColor Yellow
        # Check if virtual environment exists and is valid
        if (!(Test-Path ".\.venv_meem\Scripts\python.exe")) {
            throw "Virtual environment exists but is corrupted"
        }
    }
    
    # Activate the virtual environment:
    & ".\.venv_meem\Scripts\Activate.ps1"
    Write-Host "Virtual environment activated successfully" -ForegroundColor Green

    exit 0
}
catch {
    Write-Host "ERROR: $_" -ForegroundColor Red
    exit 1
}
