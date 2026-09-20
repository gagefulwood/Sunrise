param([string]$RetainedDirectory)

$ErrorActionPreference = 'Stop'
$questRoot = Split-Path $PSScriptRoot -Parent
$questBuild = Join-Path $questRoot 'build/quest-transition-tests'
New-Item -ItemType Directory -Path $questBuild -Force | Out-Null
$questVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$questVs = & $questVsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $questVs) { throw 'Visual Studio C++ tools are required.' }
$questVcVars = Join-Path $questVs 'VC/Auxiliary/Build/vcvars64.bat'
$questSource = Join-Path $questRoot 'Sunrise/src'
$questSqlite = Join-Path $questRoot 'Sunrise/vendor/sqlite'

$questSources = @(
    'tests/quest_transition_runtime.cpp',
    'tests/quest_transition_reader_tests.cpp',
    'tests/quest_transition_catalog_tests.cpp',
    'Sunrise/src/middleware/web_service/messages/family5_codec.cpp',
    'Sunrise/src/middleware/encoding/bit_reader.cpp',
    'Sunrise/src/middleware/encoding/bit_writer.cpp',
    'Sunrise/src/middleware/content/packages/tables/quest_initialization_reader.cpp',
    'Sunrise/src/middleware/content/packages/tables/quest_transition_reader.cpp',
    'Sunrise/src/middleware/content/packages/tables/definition_index_table.cpp',
    'Sunrise/src/state/runtime/state_quest_transition_runtime.cpp',
    'Sunrise/src/state/runtime/state_account_identity_runtime.cpp',
    'Sunrise/src/state/runtime/state_account_equipment_runtime.cpp',
    'Sunrise/src/state/build_data/items/item_catalog.cpp',
    'Sunrise/src/state/account/account_state.cpp',
    'Sunrise/src/state/account/inventory/inventory_state.cpp',
    'Sunrise/src/state/account/settings/settings_state.cpp',
    'Sunrise/src/state/investment/investment_database.cpp',
    'Sunrise/src/state/investment/investment_account_store.cpp',
    'Sunrise/src/state/investment/investment_inventory_store.cpp',
    'Sunrise/src/state/investment/investment_settings_store.cpp',
    'Sunrise/src/state/investment/investment_unlock_store.cpp'
) | ForEach-Object { '"' + (Join-Path $questRoot $_) + '"' }

Push-Location $questBuild
try {
    $questSqliteObject = Join-Path $questBuild 'sqlite3.obj'
    if (-not (Test-Path -LiteralPath $questSqliteObject) -or
        (Get-Item -LiteralPath "$questSqlite/sqlite3.c").LastWriteTimeUtc -gt
        (Get-Item -LiteralPath $questSqliteObject).LastWriteTimeUtc) {
        & cmd.exe /d /s /c "`"$questVcVars`" >nul && cl /nologo /O2 /w /TC /c /DSQLITE_OMIT_LOAD_EXTENSION /DSQLITE_THREADSAFE=1 `"$questSqlite/sqlite3.c`" /Fosqlite3.obj"
        if ($LASTEXITCODE -ne 0) { throw 'SQLite test build failed.' }
    }
    $questCommand = "`"$questVcVars`" >nul && cl /nologo /std:c++20 /EHsc /O2 /Gy /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /I`"$questSource`" /external:I`"$questSqlite`" /external:W0 " + ($questSources -join ' ') + ' sqlite3.obj /Fequest_transition_tests.exe /link /OPT:REF /STACK:8388608'
    & cmd.exe /d /s /c $questCommand
    if ($LASTEXITCODE -ne 0) { throw 'Quest transition test build failed.' }
    $questScratch = Join-Path $questBuild ('objective-progress-' + [guid]::NewGuid().ToString('N') + '.sqlite3')
    $questRetained = if ($RetainedDirectory) { $RetainedDirectory } else { '-' }
    $questArguments = @((Join-Path $questRoot 'Sunrise/resources/database'), $questRetained, $questScratch)
    & (Join-Path $questBuild 'quest_transition_tests.exe') @questArguments
    if ($LASTEXITCODE -ne 0) { throw 'Quest transition checks failed.' }
} finally {
    Pop-Location
}
