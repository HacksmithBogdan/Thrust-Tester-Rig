param(
    [Parameter(Mandatory=$true)][ValidatePattern('^COM[0-9]+$')][string]$Port,
    [ValidateSet('Old','New')][string]$Bootloader = 'Old',
    [string]$Firmware = '',
    [string]$Avrdude = '',
    [string]$AvrdudeConfig = ''
)
$ErrorActionPreference = 'Stop'
if (-not $Firmware) {
    $Firmware = Join-Path $PSScriptRoot 'firmware.hex'
    if (-not (Test-Path -LiteralPath $Firmware)) { $Firmware = Join-Path $PSScriptRoot 'artifacts\firmware.hex' }
}
$Firmware = (Resolve-Path -LiteralPath $Firmware).Path
if (-not $Avrdude) { $Avrdude = Join-Path $env:USERPROFILE '.platformio\packages\tool-avrdude\avrdude.exe' }
if (-not $AvrdudeConfig) { $AvrdudeConfig = Join-Path (Split-Path -Parent $Avrdude) 'avrdude.conf' }
if (-not (Test-Path -LiteralPath $Avrdude) -or -not (Test-Path -LiteralPath $AvrdudeConfig)) {
    throw 'AVR tools are missing. Run build.ps1 in the source project, or supply -Avrdude and -AvrdudeConfig.'
}
$rigBaud = if ($Bootloader -eq 'Old') { 57600 } else { 115200 }
$rigBackupRoot = Join-Path $PSScriptRoot ('artifacts\backups\' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + $Port)
New-Item -ItemType Directory -Path $rigBackupRoot -Force | Out-Null
$rigFlashBackup = Join-Path $rigBackupRoot 'original-flash.hex'
$rigEepromBackup = Join-Path $rigBackupRoot 'original-eeprom.bin'
$rigEepromAfter = Join-Path $rigBackupRoot 'after-upload-eeprom.bin'
$rigArguments = @('-C',$AvrdudeConfig,'-p','atmega328p','-c','arduino','-P',$Port,'-b',"$rigBaud")
Write-Output "Backing up measurement Nano on $Port ($Bootloader bootloader)."
# The MCU signature must match; do not add -F, a chip erase, fuse writes or EEPROM writes.
& $Avrdude @rigArguments -U "flash:r:${rigFlashBackup}:i" -U "eeprom:r:${rigEepromBackup}:r"
if ($LASTEXITCODE -ne 0) { throw 'Backup failed. No firmware was written.' }
if ((Get-Item -LiteralPath $rigEepromBackup).Length -ne 1024) { throw 'Incomplete EEPROM backup. No firmware was written.' }
& $Avrdude @rigArguments -D -U "flash:w:${Firmware}:i" -U "eeprom:r:${rigEepromAfter}:r"
if ($LASTEXITCODE -ne 0) { throw "Upload or verification failed. Backups: $rigBackupRoot" }
$rigBefore = [Convert]::ToBase64String([IO.File]::ReadAllBytes($rigEepromBackup))
$rigAfter = [Convert]::ToBase64String([IO.File]::ReadAllBytes($rigEepromAfter))
if ($rigBefore -ne $rigAfter) { throw "EEPROM differs after upload. Preserve and inspect $rigBackupRoot before calibration." }
Write-Output "Verified firmware and unchanged EEPROM. Backups: $rigBackupRoot"
Write-Output 'Set the ESP32 dashboard Nano baud to 115200. Startup tares the load cell; leave it unloaded.'
