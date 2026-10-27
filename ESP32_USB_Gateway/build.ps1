param([int]$Jobs = 4)
$ErrorActionPreference = 'Stop'
# ESP-IDF rejects spaces in project paths. Build a source copy in a dedicated cache.
$projectRoot = $PSScriptRoot
$toolsRoot = Join-Path $env:LOCALAPPDATA 'ThrustTesterTools'
$venvPython = Join-Path $toolsRoot 'venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $venvPython)) {
    New-Item -ItemType Directory -Path $toolsRoot -Force | Out-Null
    python -m venv (Join-Path $toolsRoot 'venv')
    if ($LASTEXITCODE -ne 0) { throw 'Could not create the Python tools environment.' }
}
& $venvPython -m pip show platformio *> $null
if ($LASTEXITCODE -ne 0) {
    & $venvPython -m pip install --disable-pip-version-check platformio==6.1.18
    if ($LASTEXITCODE -ne 0) { throw 'Could not install PlatformIO.' }
}
$buildRoot = Join-Path $env:USERPROFILE '.cache\thrust-tester\esp32-usb-gateway'
if ($buildRoot.Contains(' ')) { throw 'Set USERPROFILE or use a project/build path without spaces for ESP-IDF.' }
New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
$sourceItems = @('main','web','boards','CMakeLists.txt','platformio.ini','sdkconfig.defaults','partitions.csv')
if (Test-Path -LiteralPath (Join-Path $projectRoot 'dependencies.lock')) { $sourceItems += 'dependencies.lock' }
foreach ($sourceItem in $sourceItems) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $sourceItem) -Destination $buildRoot -Recurse -Force
}
Push-Location $buildRoot
try {
    & $venvPython -m platformio run --jobs $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'ESP32 firmware build failed.' }
} finally { Pop-Location }
$artifactRoot = Join-Path $projectRoot 'artifacts'
New-Item -ItemType Directory -Path $artifactRoot -Force | Out-Null
$outputRoot = Join-Path $buildRoot '.pio\build\esp32-s3-zero'
foreach ($binary in @('firmware.bin','bootloader.bin','partitions.bin')) {
    Copy-Item -LiteralPath (Join-Path $outputRoot $binary) -Destination $artifactRoot -Force
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'flash.ps1') -Destination $artifactRoot -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'FLASHING.txt') -Destination $artifactRoot -Force
if (Test-Path -LiteralPath (Join-Path $buildRoot 'dependencies.lock')) {
    $lockText = [System.IO.File]::ReadAllText((Join-Path $buildRoot 'dependencies.lock')).Replace("`r`n", "`n")
    [System.IO.File]::WriteAllText((Join-Path $projectRoot 'dependencies.lock'), $lockText, [System.Text.UTF8Encoding]::new($false))
}
$binaryHashes = Get-ChildItem -LiteralPath $artifactRoot -Filter '*.bin' | Get-FileHash -Algorithm SHA256
$binaryHashes | ForEach-Object { $_.Hash + '  ' + [System.IO.Path]::GetFileName($_.Path) } | Set-Content -LiteralPath (Join-Path $artifactRoot 'SHA256SUMS.txt') -Encoding ascii
$packageItems = @('firmware.bin','bootloader.bin','partitions.bin','flash.ps1','FLASHING.txt','SHA256SUMS.txt') | ForEach-Object { Join-Path $artifactRoot $_ }
Compress-Archive -LiteralPath $packageItems -DestinationPath (Join-Path $artifactRoot 'thrust-tester-esp32-s3-zero.zip') -Force
Write-Output "Firmware package: $artifactRoot\thrust-tester-esp32-s3-zero.zip"
