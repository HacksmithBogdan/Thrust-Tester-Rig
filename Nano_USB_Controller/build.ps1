param([int]$Jobs = 4)
$ErrorActionPreference = 'Stop'
$rigToolsRoot = Join-Path $env:LOCALAPPDATA 'ThrustTesterTools'
$rigPython = Join-Path $rigToolsRoot 'venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $rigPython)) {
    New-Item -ItemType Directory -Path $rigToolsRoot -Force | Out-Null
    python -m venv (Join-Path $rigToolsRoot 'venv')
    if ($LASTEXITCODE -ne 0) { throw 'Could not create the Python tools environment.' }
}
& $rigPython -m pip show platformio *> $null
if ($LASTEXITCODE -ne 0) {
    & $rigPython -m pip install --disable-pip-version-check platformio==6.1.18
    if ($LASTEXITCODE -ne 0) { throw 'Could not install PlatformIO.' }
}
& $rigPython -m platformio run --project-dir $PSScriptRoot --environment nanoatmega328 --jobs $Jobs
if ($LASTEXITCODE -ne 0) { throw 'Measurement Nano build failed.' }
$rigArtifacts = Join-Path $PSScriptRoot 'artifacts'
New-Item -ItemType Directory -Path $rigArtifacts -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '.pio\build\nanoatmega328\firmware.hex') -Destination $rigArtifacts -Force
foreach ($rigFile in @('flash.ps1','README.md')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $rigFile) -Destination $rigArtifacts -Force
}
$rigHash = Get-FileHash -LiteralPath (Join-Path $rigArtifacts 'firmware.hex') -Algorithm SHA256
($rigHash.Hash + '  firmware.hex') | Set-Content -LiteralPath (Join-Path $rigArtifacts 'SHA256SUMS.txt') -Encoding ascii
# Package only named deliverables: local device backups and test captures stay private.
$rigPackageFiles = @('firmware.hex','flash.ps1','README.md','SHA256SUMS.txt') | ForEach-Object { Join-Path $rigArtifacts $_ }
Compress-Archive -LiteralPath $rigPackageFiles -DestinationPath (Join-Path $rigArtifacts 'thrust-tester-measurement-nano.zip') -Force
Write-Output "Firmware package: $rigArtifacts\thrust-tester-measurement-nano.zip"
