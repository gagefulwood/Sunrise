$ErrorActionPreference = 'Stop'
$bundleRoot = Split-Path $PSScriptRoot -Parent
$bundleBuild = Join-Path $bundleRoot 'build/cosmetic-reward-tests'
New-Item -ItemType Directory -Path $bundleBuild -Force | Out-Null
$bundleVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$bundleVs = & $bundleVsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $bundleVs) { throw 'Visual Studio C++ tools are required.' }
$bundleVcVars = Join-Path $bundleVs 'VC/Auxiliary/Build/vcvars64.bat'
$bundleSource = Join-Path $bundleRoot 'Sunrise/src'
$bundleSqlite = Join-Path $bundleRoot 'Sunrise/vendor/sqlite'
$bundleSources = @(
    'tests/cosmetic_rewards.cpp',
    'Sunrise/src/state/runtime/state_account_reward_grant_runtime.cpp',
    'Sunrise/src/state/runtime/state_account_acquisition_runtime.cpp',
    'Sunrise/src/state/runtime/state_account_profile_runtime.cpp',
    'Sunrise/src/state/runtime/state_account_equipment_runtime.cpp',
    'Sunrise/src/state/runtime/state_account_identity_runtime.cpp',
    'Sunrise/src/state/account/account_state.cpp',
    'Sunrise/src/state/account/inventory/inventory_state.cpp',
    'Sunrise/src/state/account/settings/settings_state.cpp',
    'Sunrise/src/state/investment/investment_database.cpp',
    'Sunrise/src/state/investment/investment_account_store.cpp',
    'Sunrise/src/state/investment/investment_inventory_store.cpp',
    'Sunrise/src/state/investment/investment_settings_store.cpp',
    'Sunrise/src/state/investment/investment_unlock_store.cpp'
) | ForEach-Object { '"' + (Join-Path $bundleRoot $_) + '"' }
Push-Location $bundleBuild
try {
    $bundleSqliteObject = Join-Path $bundleBuild 'sqlite3.obj'
    if (-not (Test-Path -LiteralPath $bundleSqliteObject)) {
        & cmd.exe /d /s /c "`"$bundleVcVars`" >nul && cl /nologo /O2 /w /TC /c /DSQLITE_OMIT_LOAD_EXTENSION /DSQLITE_THREADSAFE=1 `"$bundleSqlite/sqlite3.c`" /Fosqlite3.obj"
        if ($LASTEXITCODE -ne 0) { throw 'SQLite test build failed.' }
    }
    $bundleCommand = "`"$bundleVcVars`" >nul && cl /nologo /std:c++20 /EHsc /O2 /Gy /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /I`"$bundleSource`" /external:I`"$bundleSqlite`" /external:W0 " + ($bundleSources -join ' ') + ' sqlite3.obj /Fecosmetic_rewards.exe /link /OPT:REF /STACK:8388608'
    & cmd.exe /d /s /c $bundleCommand
    if ($LASTEXITCODE -ne 0) { throw 'Cosmetic test build failed.' }
    $bundleScratch = Join-Path $bundleBuild ('cosmetic-' + [guid]::NewGuid().ToString('N') + '.sqlite3')
    & (Join-Path $bundleBuild 'cosmetic_rewards.exe') (Join-Path $bundleRoot 'Sunrise/resources/database') $bundleScratch
    if ($LASTEXITCODE -ne 0) { throw 'Cosmetic checks failed.' }
} finally {
    Pop-Location
}
