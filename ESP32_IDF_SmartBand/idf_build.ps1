Write-Host "Setting up ESP-IDF Environment..."
. "C:\Users\mjnhp\esp\v5.3.4\esp-idf\export.ps1"
Write-Host "Building, Flashing, and Monitoring..."
idf.py build flash monitor
