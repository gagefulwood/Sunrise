$ErrorActionPreference = 'Stop'
$bundleRoot = Split-Path $PSScriptRoot -Parent
$bundleBuild = Join-Path $bundleRoot 'build/vendor-bundle-tests'
New-Item -ItemType Directory -Path $bundleBuild -Force | Out-Null
$bundleVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$bundleVs = & $bundleVsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $bundleVs) { throw 'Visual Studio C++ tools are required.' }
$bundleVcVars = Join-Path $bundleVs 'VC/Auxiliary/Build/vcvars64.bat'
$bundleSource = Join-Path $bundleRoot 'Sunrise/src'
$bundleSqlite = Join-Path $bundleRoot 'Sunrise/vendor/sqlite'
# Reserve 8 MiB for stack-local account snapshots and prepared-grant fixtures.
$bundleStackBytes = 8MB
$bundleSources = @(
    'tests/vendor_bundles.cpp',
    'Sunrise/src/state/runtime/state_vendor_bundle_runtime.cpp',
    'Sunrise/src/state/build_data/vendors/bundle_catalog.cpp',
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
    $bundleContentSources = @(
        'tests/vendor_bundle_content.cpp',
        'Sunrise/src/middleware/content/packages/tables/vendor_bundle_reader.cpp',
        'Sunrise/src/middleware/content/packages/tables/item_bundle_reader.cpp',
        'Sunrise/src/state/build_data/cache/records/cache_investment_records.cpp',
        'Sunrise/src/state/build_data/season_pass/season_pass_catalog.cpp',
        'Sunrise/src/middleware/content/packages/tables/definition_index_table.cpp'
    ) | ForEach-Object { '"' + (Join-Path $bundleRoot $_) + '"' }
    $bundleContentCommand = "`"$bundleVcVars`" >nul && cl /nologo /std:c++20 /EHsc /O2 /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /I`"$bundleSource`" " + ($bundleContentSources -join ' ') + ' /Fevendor_bundle_content.exe'
    & cmd.exe /d /s /c $bundleContentCommand
    if ($LASTEXITCODE -ne 0) { throw 'Bundle content test build failed.' }
    & (Join-Path $bundleBuild 'vendor_bundle_content.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Bundle content checks failed.' }
    $bundleSqliteObject = Join-Path $bundleBuild 'sqlite3.obj'
    if (-not (Test-Path -LiteralPath $bundleSqliteObject)) {
        & cmd.exe /d /s /c "`"$bundleVcVars`" >nul && cl /nologo /O2 /w /TC /c /DSQLITE_OMIT_LOAD_EXTENSION /DSQLITE_THREADSAFE=1 `"$bundleSqlite/sqlite3.c`" /Fosqlite3.obj"
        if ($LASTEXITCODE -ne 0) { throw 'SQLite test build failed.' }
    }
    $bundleCommand = "`"$bundleVcVars`" >nul && cl /nologo /std:c++20 /EHsc /O2 /Gy /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /I`"$bundleSource`" /external:I`"$bundleSqlite`" /external:W0 " + ($bundleSources -join ' ') + " sqlite3.obj /Fevendor_bundles.exe /link /OPT:REF /STACK:$bundleStackBytes"
    & cmd.exe /d /s /c $bundleCommand
    if ($LASTEXITCODE -ne 0) { throw 'Bundle test build failed.' }
    $bundleScratch = Join-Path $bundleBuild ('bundle-' + [guid]::NewGuid().ToString('N') + '.sqlite3')
    & (Join-Path $bundleBuild 'vendor_bundles.exe') (Join-Path $bundleRoot 'Sunrise/resources/database') $bundleScratch
    if ($LASTEXITCODE -ne 0) { throw 'Bundle checks failed.' }
} finally {
    Pop-Location
}
