$ErrorActionPreference = 'Stop'
$engramRoot = Split-Path $PSScriptRoot -Parent
$engramBuild = Join-Path $engramRoot 'build/engram-decryption-tests'
New-Item -ItemType Directory -Path $engramBuild -Force | Out-Null
$engramVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$engramVs = & $engramVsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $engramVs) { throw 'Visual Studio C++ tools are required.' }
$engramVcVars = Join-Path $engramVs 'VC/Auxiliary/Build/vcvars64.bat'
$engramSource = Join-Path $engramRoot 'Sunrise/src'
$engramSqlite = Join-Path $engramRoot 'Sunrise/vendor/sqlite'
$engramSources = @(
    'tests/engram_decryption_tests.cpp',
    'Sunrise/src/middleware/datagen/family4/instance/instance_encoder.cpp',
    'Sunrise/src/state/runtime/state_account_acquisition_runtime.cpp',
    'Sunrise/src/state/runtime/state_account_identity_runtime.cpp',
    'Sunrise/src/state/runtime/state_account_equipment_runtime.cpp',
    'Sunrise/src/state/runtime/state_account_profile_runtime.cpp',
    'Sunrise/src/state/account/account_state.cpp',
    'Sunrise/src/state/account/inventory/inventory_state.cpp',
    'Sunrise/src/state/account/settings/settings_state.cpp',
    'Sunrise/src/state/investment/investment_database.cpp',
    'Sunrise/src/state/investment/investment_account_store.cpp',
    'Sunrise/src/state/investment/investment_inventory_store.cpp',
    'Sunrise/src/state/investment/investment_settings_store.cpp',
    'Sunrise/src/state/investment/investment_unlock_store.cpp',
    'Sunrise/src/server/bap/encrypted/queuez/queuez_state_validation.cpp',
    'Sunrise/src/server/bap/encrypted/queuez/staging/queuez_character_staging.cpp'
) | ForEach-Object { '"' + (Join-Path $engramRoot $_) + '"' }
Push-Location $engramBuild
try {
    if (-not (Test-Path -LiteralPath 'sqlite3.obj') -or
        (Get-Item -LiteralPath "$engramSqlite/sqlite3.c").LastWriteTimeUtc -gt
        (Get-Item -LiteralPath 'sqlite3.obj').LastWriteTimeUtc) {
        & cmd.exe /d /s /c "`"$engramVcVars`" >nul && cl /nologo /O2 /w /TC /c /DSQLITE_OMIT_LOAD_EXTENSION /DSQLITE_THREADSAFE=1 `"$engramSqlite/sqlite3.c`" /Fosqlite3.obj"
        if ($LASTEXITCODE -ne 0) { throw 'SQLite test build failed.' }
    }
    $engramCommand = "`"$engramVcVars`" >nul && cl /nologo /std:c++20 /EHsc /O2 /Gy /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /I`"$engramSource`" /external:I`"$engramSqlite`" /external:W0 " + ($engramSources -join ' ') + ' sqlite3.obj /Feengram_decryption_tests.exe /link /OPT:REF /STACK:8388608'
    & cmd.exe /d /s /c $engramCommand
    if ($LASTEXITCODE -ne 0) { throw 'Engram test build failed.' }
    & (Join-Path $engramBuild 'engram_decryption_tests.exe') (Join-Path $engramRoot 'Sunrise/resources/database')
    if ($LASTEXITCODE -ne 0) { throw 'Engram checks failed.' }
} finally { Pop-Location }
