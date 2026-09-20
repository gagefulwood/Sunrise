$ErrorActionPreference = 'Stop'
$rewardRoot = Split-Path $PSScriptRoot -Parent
$rewardBuild = Join-Path $rewardRoot 'build/reward-site-catalog-tests'
New-Item -ItemType Directory -Path $rewardBuild -Force | Out-Null
$rewardVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$rewardVs = & $rewardVsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $rewardVs) { throw 'Visual Studio C++ tools are required.' }
$rewardVcVars = Join-Path $rewardVs 'VC/Auxiliary/Build/vcvars64.bat'
$rewardSource = Join-Path $rewardRoot 'Sunrise/src'
$rewardSqlite = Join-Path $rewardRoot 'Sunrise/vendor/sqlite'

$rewardSources = @(
    'tests/reward_site_catalog_tests.cpp',
    'Sunrise/src/state/build_data/reward_sites/reward_site_catalog.cpp'
) | ForEach-Object { '"' + (Join-Path $rewardRoot $_) + '"' }

Push-Location $rewardBuild
try {
    $rewardSqliteObject = Join-Path $rewardBuild 'sqlite3.obj'
    if (-not (Test-Path -LiteralPath $rewardSqliteObject) -or
        (Get-Item -LiteralPath "$rewardSqlite/sqlite3.c").LastWriteTimeUtc -gt
        (Get-Item -LiteralPath $rewardSqliteObject).LastWriteTimeUtc) {
        & cmd.exe /d /s /c "`"$rewardVcVars`" >nul && cl /nologo /O2 /w /TC /c /DSQLITE_OMIT_LOAD_EXTENSION /DSQLITE_THREADSAFE=1 `"$rewardSqlite/sqlite3.c`" /Fosqlite3.obj"
        if ($LASTEXITCODE -ne 0) { throw 'SQLite test build failed.' }
    }
    $rewardCommand = "`"$rewardVcVars`" >nul && cl /nologo /std:c++20 /EHsc /O2 /Gy /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /I`"$rewardSource`" /external:I`"$rewardSqlite`" /external:W0 " + ($rewardSources -join ' ') + ' sqlite3.obj /Fereward_site_catalog_tests.exe /link /OPT:REF'
    & cmd.exe /d /s /c $rewardCommand
    if ($LASTEXITCODE -ne 0) { throw 'Reward Site catalog test build failed.' }
    & (Join-Path $rewardBuild 'reward_site_catalog_tests.exe') (Join-Path $rewardRoot 'Sunrise/resources/database')
    if ($LASTEXITCODE -ne 0) { throw 'Reward Site catalog checks failed.' }
} finally {
    Pop-Location
}
