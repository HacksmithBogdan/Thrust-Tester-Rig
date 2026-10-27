param([string]$Python = 'python')
$ErrorActionPreference = 'Stop'
# Constrain the temporary compiler and its children rather than occupying every CPU.
try { (Get-Process -Id $PID).ProcessorAffinity = [IntPtr]15 } catch { }
$projectRoot = Split-Path -Parent $PSScriptRoot
$idfRoot = Join-Path $env:USERPROFILE '.platformio\packages\framework-espidf'
$jsonRoot = Join-Path $idfRoot 'components\json\cJSON'
if (-not (Test-Path -LiteralPath (Join-Path $jsonRoot 'cJSON.c'))) { throw 'Build the firmware once to install ESP-IDF/cJSON first.' }
& $Python -m ziglang version
if ($LASTEXITCODE -ne 0) { throw 'Install the test compiler into your Python environment: python -m pip install ziglang==0.14.1' }
$testOutput = Join-Path $projectRoot 'artifacts\host-tests'
New-Item -ItemType Directory -Path $testOutput -Force | Out-Null
$testExe = Join-Path $testOutput 'telemetry-test.exe'
$jsonObject = Join-Path $testOutput 'cJSON.o'
& $Python -m ziglang cc -O1 -c (Join-Path $jsonRoot 'cJSON.c') -o $jsonObject
if ($LASTEXITCODE -ne 0) { throw 'Compiling the JSON dependency failed.' }
& $Python -m ziglang c++ -std=c++17 -O1 -I (Join-Path $projectRoot 'main') -I (Join-Path $PSScriptRoot 'stubs') -I $jsonRoot (Join-Path $projectRoot 'main\telemetry.cpp') (Join-Path $PSScriptRoot 'telemetry_test.cpp') $jsonObject -o $testExe
if ($LASTEXITCODE -ne 0) { throw 'Compiling host telemetry tests failed.' }
& $testExe
if ($LASTEXITCODE -ne 0) { throw 'Telemetry tests failed.' }
