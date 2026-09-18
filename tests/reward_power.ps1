$ErrorActionPreference = 'Stop'
$rewardRoot = Split-Path $PSScriptRoot -Parent
$rewardBuild = Join-Path $rewardRoot 'build/reward-power-tests'
New-Item -ItemType Directory -Path $rewardBuild -Force | Out-Null
$rewardVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$rewardVs = & $rewardVsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $rewardVs) { throw 'Visual Studio C++ tools are required.' }
$rewardVcVars = Join-Path $rewardVs 'VC/Auxiliary/Build/vcvars64.bat'
$rewardSource = Join-Path $rewardRoot 'Sunrise/src'
Push-Location $rewardBuild
try {
    & cmd.exe /d /s /c "`"$rewardVcVars`" >nul && cl /nologo /std:c++20 /EHsc /W4 /WX /DWIN32_LEAN_AND_MEAN /DNOMINMAX /I`"$rewardSource`" `"$rewardRoot/tests/reward_power_tests.cpp`" `"$rewardSource/state/equipment/light/calculation/equipment_light_calculation.cpp`" /Fereward_power_tests.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Reward Power test build failed.' }
    & (Join-Path $rewardBuild 'reward_power_tests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Reward Power checks failed.' }
} finally { Pop-Location }
