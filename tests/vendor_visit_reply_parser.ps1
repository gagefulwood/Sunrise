param([Parameter(Mandatory = $true)][string]$RetainedDirectory)

$ErrorActionPreference = 'Stop'
$vendorRoot = Split-Path $PSScriptRoot -Parent
$vendorBuild = Join-Path $vendorRoot 'build/vendor-visit-reply-tests'
New-Item -ItemType Directory -Path $vendorBuild -Force | Out-Null
$vendorVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vendorVs = & $vendorVsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vendorVs) { throw 'Visual Studio C++ tools are required.' }
$vendorVcVars = Join-Path $vendorVs 'VC/Auxiliary/Build/vcvars64.bat'
$vendorSource = Join-Path $vendorRoot 'Sunrise/src'
$vendorSources = @(
    'tests/vendor_visit_reply_parser.cpp',
    'Sunrise/src/client/content/vendors/visit_reply_parser.cpp',
    'Sunrise/src/middleware/content/packages/tables/definition_index_table.cpp',
    'Sunrise/src/state/build_data/cache/records/cache_vendor_records.cpp',
    'Sunrise/src/state/build_data/vendors/vendor_catalog.cpp'
) | ForEach-Object { '"' + (Join-Path $vendorRoot $_) + '"' }

Push-Location $vendorBuild
try {
    $vendorCommand = '"' + $vendorVcVars + '" >nul && cl /nologo /std:c++20 /EHsc /O2 /Gy /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /I"' + $vendorSource + '" ' + ($vendorSources -join ' ') + ' /Fevendor_visit_reply_parser_tests.exe /link /OPT:REF'
    & cmd.exe /d /s /c $vendorCommand
    if ($LASTEXITCODE -ne 0) { throw 'Vendor visit reply parser test build failed.' }
    & (Join-Path $vendorBuild 'vendor_visit_reply_parser_tests.exe') $RetainedDirectory
    if ($LASTEXITCODE -ne 0) { throw 'Vendor visit reply parser checks failed.' }
} finally {
    Pop-Location
}
