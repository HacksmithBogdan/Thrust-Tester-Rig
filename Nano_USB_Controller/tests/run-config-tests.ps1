param([string]$Python = 'python')
$ErrorActionPreference = 'Stop'
& $Python -m ziglang version
if ($LASTEXITCODE -ne 0) { throw 'Install the native test compiler: python -m pip install ziglang==0.14.1' }
$rigProject = Split-Path -Parent $PSScriptRoot
$rigOutput = Join-Path $rigProject 'artifacts\host-tests'
New-Item -ItemType Directory -Path $rigOutput -Force | Out-Null
$rigTest = Join-Path $rigOutput 'config-test.exe'
& $Python -m ziglang c++ -std=c++17 -O1 -I (Join-Path $rigProject 'main') (Join-Path $PSScriptRoot 'config_test.cpp') -o $rigTest
if ($LASTEXITCODE -ne 0) { throw 'Native Nano test build failed.' }
& $rigTest
if ($LASTEXITCODE -ne 0) { throw 'Native Nano tests failed.' }
