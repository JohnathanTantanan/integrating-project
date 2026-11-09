$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
New-Item -ItemType Directory -Force build | Out-Null

if (-not $env:HIP_PATH) { $env:HIP_PATH = [Environment]::GetEnvironmentVariable('HIP_PATH', 'Machine') }
if (-not $env:HIP_PATH) { throw 'HIP_PATH is not set; install the HIP SDK 6.2.4 first' }

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs2022 = & $vswhere -products * -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
if (-not $vs2022) { throw 'Visual Studio 2022 C++ tools not found; HIP SDK 6.2 needs the 2022 toolset' }
$vcvars = Join-Path $vs2022 'VC\Auxiliary\Build\vcvars64.bat'
cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
  if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
}

$hipcc = Join-Path $env:HIP_PATH 'bin\hipcc.bin.exe'
& $hipcc --offload-arch=gfx1032 -O2 -o build\hello.exe engine\hip\hello.hip
if ($LASTEXITCODE -ne 0) { throw 'hello.hip failed to build' }
& $hipcc --offload-arch=gfx1032 -O2 -std=c++17 -DWITH_HIP -D_CRT_SECURE_NO_WARNINGS -o build\sagev-batch.exe engine\main.cpp engine\hip\gpu.hip
if ($LASTEXITCODE -ne 0) { throw 'sagev-batch failed to build' }
Write-Output "built build\hello.exe and build\sagev-batch.exe with $vs2022"
