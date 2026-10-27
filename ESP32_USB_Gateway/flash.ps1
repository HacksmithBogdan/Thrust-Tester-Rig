param([Parameter(Mandatory=$true)][string]$Port)
$ErrorActionPreference = 'Stop'
$binaryRoot = if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'firmware.bin')) { $PSScriptRoot } else { Join-Path $PSScriptRoot 'artifacts' }
foreach ($binary in @('bootloader.bin','partitions.bin','firmware.bin')) {
    if (-not (Test-Path -LiteralPath (Join-Path $binaryRoot $binary))) { throw "Missing $binary. Run build.ps1 first, or extract the complete firmware ZIP." }
}
$toolsRoot = Join-Path $env:LOCALAPPDATA 'ThrustTesterTools'
$venvPython = Join-Path $toolsRoot 'flash-venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $venvPython)) {
    python -m venv (Join-Path $toolsRoot 'flash-venv')
    if ($LASTEXITCODE -ne 0) { throw 'Could not create flashing tools environment.' }
}
& $venvPython -m pip show esptool *> $null
if ($LASTEXITCODE -ne 0) {
    & $venvPython -m pip install --disable-pip-version-check esptool==4.9.0
    if ($LASTEXITCODE -ne 0) { throw 'Could not install esptool.' }
}
& $venvPython -m esptool --chip esp32s3 --port $Port --baud 460800 --before default_reset --after hard_reset write_flash --flash_size 16MB 0x0 (Join-Path $binaryRoot 'bootloader.bin') 0x8000 (Join-Path $binaryRoot 'partitions.bin') 0x10000 (Join-Path $binaryRoot 'firmware.bin')
if ($LASTEXITCODE -ne 0) { throw 'Flashing failed. Hold BOOT, press RESET, release BOOT, and check the COM port.' }
Write-Output 'Flashed LCD 2.8B. Fresh installations start in PC service mode. Select Nano USB on the Device screen when moving the cable to the Nano.'
