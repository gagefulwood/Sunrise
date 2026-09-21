$ErrorActionPreference = 'Stop'
$cacheRoot = Split-Path $PSScriptRoot -Parent
$cacheBuild = Join-Path $cacheRoot 'build/build-data-cache-tests'
New-Item -ItemType Directory -Path $cacheBuild -Force | Out-Null
$cacheVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$cacheVs = & $cacheVsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $cacheVs) { throw 'Visual Studio C++ tools are required.' }
$cacheVcVars = Join-Path $cacheVs 'VC/Auxiliary/Build/vcvars64.bat'
$cacheSource = Join-Path $cacheRoot 'Sunrise/src'

$cacheSources = @(
    'tests/build_data_cache_tests.cpp',
    'Sunrise/src/state/build_data/cache/read/cache_file_reader.cpp',
    'Sunrise/src/state/build_data/cache/write/cache_file_writer.cpp',
    'Sunrise/src/state/build_data/cache/write/temporary/temporary_cache_file.cpp',
    'Sunrise/src/state/build_data/cache/write/validation/cache_file_comparison.cpp',
    'Sunrise/src/core/filesystem/path.cpp',
    'Sunrise/src/core/filesystem/temporary_sibling.cpp'
) | ForEach-Object { '"' + (Join-Path $cacheRoot $_) + '"' }

Push-Location $cacheBuild
try {
    $cacheCommand = '"' + $cacheVcVars + '" >nul && cl /nologo /std:c++20 /EHsc /O2 /Gy /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /I"' + $cacheSource + '" ' + ($cacheSources -join ' ') + ' /Febuild_data_cache_tests.exe /link /OPT:REF'
    & cmd.exe /d /s /c $cacheCommand
    if ($LASTEXITCODE -ne 0) { throw 'Build-data cache test build failed.' }
    $cacheScratch = Join-Path $cacheBuild ('scratch-' + [guid]::NewGuid().ToString('N'))
    & (Join-Path $cacheBuild 'build_data_cache_tests.exe') $cacheScratch
    if ($LASTEXITCODE -ne 0) { throw 'Build-data cache checks failed.' }
} finally {
    Pop-Location
}
