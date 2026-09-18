param([string]$ItemDefinition, [string]$QualityCaps, [string]$ExpectedCap,
      [string]$IdentityTable, [string]$ExpectedClass)

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
    $rewardSources = @(
        "$rewardRoot/tests/reward_power_tests.cpp",
        "$rewardRoot/tests/item_requirement_tests.cpp",
        "$rewardSource/state/build_data/vendors/vendor_expression.cpp",
        "$rewardSource/state/equipment/light/calculation/equipment_light_calculation.cpp",
        "$rewardSource/state/equipment/light/resolution/configured_equipment_light_resolver.cpp",
        "$rewardSource/state/account/inventory/inventory_state.cpp",
        "$rewardSource/state/build_data/cache/records/cache_record_codec.cpp",
        "$rewardSource/middleware/content/packages/tables/item_definition_reader.cpp",
        "$rewardSource/middleware/content/packages/tables/item_requirement_reader.cpp",
        "$rewardSource/middleware/content/packages/tables/item_appearance_reader.cpp",
        "$rewardSource/middleware/content/packages/tables/definition_index_table.cpp"
    ) | ForEach-Object { '"' + $_ + '"' }
    $rewardCommand = "`"$rewardVcVars`" >nul && cl /nologo /std:c++20 /EHsc /Gy /W4 /WX /DWIN32_LEAN_AND_MEAN /DNOMINMAX /I`"$rewardSource`" " + ($rewardSources -join ' ') + ' /Fereward_power_tests.exe /link /OPT:REF /STACK:8388608'
    & cmd.exe /d /s /c $rewardCommand
    if ($LASTEXITCODE -ne 0) { throw 'Reward Power test build failed.' }
    $rewardArguments = @()
    if ($ItemDefinition -or $QualityCaps -or $ExpectedCap) {
        if (-not ($ItemDefinition -and $QualityCaps -and $ExpectedCap)) {
            throw 'Provide ItemDefinition, QualityCaps and ExpectedCap together.'
        }
        $rewardArguments = @($ItemDefinition, $QualityCaps, $ExpectedCap)
    }
    if ($IdentityTable -or $ExpectedClass) {
        if (-not ($rewardArguments.Count -eq 3 -and $IdentityTable -and $ExpectedClass)) {
            throw 'IdentityTable and ExpectedClass also require the item and cap arguments.'
        }
        $rewardArguments += @($IdentityTable, $ExpectedClass)
    }
    & (Join-Path $rewardBuild 'reward_power_tests.exe') @rewardArguments
    if ($LASTEXITCODE -ne 0) { throw 'Reward Power checks failed.' }
} finally { Pop-Location }
