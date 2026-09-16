$ErrorActionPreference = 'Stop'
$pursuitRoot = Split-Path $PSScriptRoot -Parent
$pursuitBuild = Join-Path $pursuitRoot 'build/pursuit-hold-tests'
New-Item -ItemType Directory -Path $pursuitBuild -Force | Out-Null
$pursuitVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$pursuitVs = & $pursuitVsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $pursuitVs) { throw 'Visual Studio C++ tools are required.' }
$pursuitVcVars = Join-Path $pursuitVs 'VC/Auxiliary/Build/vcvars64.bat'
$pursuitSource = Join-Path $pursuitRoot 'Sunrise/src'
Push-Location $pursuitBuild
try {
    & cmd.exe /d /s /c "`"$pursuitVcVars`" >nul && cl /nologo /std:c++20 /EHsc /W4 /WX /DWIN32_LEAN_AND_MEAN /DNOMINMAX /I`"$pursuitSource`" `"$pursuitRoot/tests/pursuit_hold_tests.cpp`" `"$pursuitSource/state/account/pursuit_hold.cpp`" /Fepursuit_hold_tests.exe /link /STACK:8388608"
    if ($LASTEXITCODE -ne 0) { throw 'Pursuit hold test build failed.' }
    & (Join-Path $pursuitBuild 'pursuit_hold_tests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Pursuit hold checks failed.' }
} finally { Pop-Location }
