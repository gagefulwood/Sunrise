$ErrorActionPreference = 'Stop'
$repRoot = Split-Path $PSScriptRoot -Parent
$repBuild = Join-Path $repRoot 'build/reputation-increment-tests'
New-Item -ItemType Directory -Path $repBuild -Force | Out-Null
$repVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$repVs = & $repVsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $repVs) { throw 'Visual Studio C++ tools are required.' }
$repVcVars = Join-Path $repVs 'VC/Auxiliary/Build/vcvars64.bat'
$repSource = Join-Path $repRoot 'Sunrise/src'
$repSources = @(
    'tests/reputation_increment.cpp',
    'Sunrise/src/server/bap/encrypted/queuez/queuez_state_validation.cpp',
    'Sunrise/src/server/bap/encrypted/queuez/staging/queuez_family_refresh_staging.cpp'
) | ForEach-Object { '"' + (Join-Path $repRoot $_) + '"' }
Push-Location $repBuild
try {
    $repCommand = '"' + $repVcVars + '" >nul && cl /nologo /std:c++20 /EHsc /O2 /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /I"' + $repSource + '" ' + ($repSources -join ' ') + ' /Fereputation_increment.exe /link /STACK:8388608'
    & cmd.exe /d /s /c $repCommand
    if ($LASTEXITCODE -ne 0) { throw 'Publication test build failed.' }
    & (Join-Path $repBuild 'reputation_increment.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Publication checks failed.' }
} finally {
    Pop-Location
}
