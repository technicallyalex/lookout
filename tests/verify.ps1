param(
    [ValidateSet('All', 'Quick', 'Native', 'Desktop')]
    [string] $Scope = 'All',
    [string] $ObsBin = (Join-Path $env:ProgramFiles 'obs-studio/bin/64bit')
)

$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$configuration = 'RelWithDebInfo'
$nativeBuild = Join-Path $project 'build'
$quickBuild = Join-Path $project 'build-quick'

function Get-BuildCache([string] $Build) {
    $path = Join-Path $Build 'CMakeCache.txt'
    if (!(Test-Path -LiteralPath $path)) {
        throw "Configure $Build first using the commands in lookout's README."
    }
    $cache = @{}
    foreach ($line in Get-Content -LiteralPath $path) {
        if ($line -match '^(CMAKE_COMMAND|CMAKE_CTEST_COMMAND|Qt6_DIR):[^=]+=(.+)$') {
            $cache[$Matches[1]] = $Matches[2]
        }
    }
    return $cache
}

function Check-Exit([string] $Step) {
    if ($LASTEXITCODE -ne 0) { throw "$Step failed (exit $LASTEXITCODE)." }
}

function Use-ObsRuntime {
    if (!(Test-Path -LiteralPath (Join-Path $ObsBin 'obs.dll'))) {
        throw "OBS runtime missing at $ObsBin. Use -ObsBin to specify the OBS bin/64bit folder."
    }
    $tempDir = Join-Path $nativeBuild 'test-temp'
    New-Item -ItemType Directory -Path $tempDir -Force | Out-Null
    $env:PATH = "$ObsBin;$($previous.PATH)"
    $env:QT_PLUGIN_PATH = $ObsBin
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $ObsBin 'platforms'
    $env:QML_IMPORT_PATH = ''
    $env:TEMP = $env:TMP = $tempDir
}

function Invoke-Checks([string] $Build, [string] $Name) {
    $cache = Get-BuildCache $Build
    & $cache.CMAKE_COMMAND --build $Build --config $configuration --parallel 1
    Check-Exit "$Name build"
    & $cache.CMAKE_CTEST_COMMAND --test-dir $Build -C $configuration --parallel 4 --output-on-failure
    Check-Exit "$Name tests"
}

$qaVariables = 'SENTINEL_QUICK_QA', 'SENTINEL_GPU_CAPTURE', 'SENTINEL_BLUR_CAPTURE',
    'SENTINEL_WGC_QA', 'SENTINEL_REPLAY_QA', 'SENTINEL_SCREENSHOT', 'SENTINEL_OBS_ROOT'
$previous = @{}
foreach ($name in $qaVariables + @('PATH', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH',
        'QML_IMPORT_PATH', 'TEMP', 'TMP', 'MSBUILDDISABLENODEREUSE')) {
    $previous[$name] = [Environment]::GetEnvironmentVariable($name)
}

try {
    foreach ($name in $qaVariables) { [Environment]::SetEnvironmentVariable($name, $null) }
    $env:MSBUILDDISABLENODEREUSE = '1'
    if ($Scope -in 'All', 'Quick') {
        $cache = Get-BuildCache $quickBuild
        $qt = (Get-Item -LiteralPath $cache.Qt6_DIR).Parent.Parent.Parent.FullName
        $env:PATH = "$(Join-Path $qt 'bin');$($previous.PATH)"
        $env:QT_PLUGIN_PATH = Join-Path $qt 'plugins'
        $env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $qt 'plugins/platforms'
        $env:QML_IMPORT_PATH = Join-Path $qt 'qml'
        Invoke-Checks $quickBuild 'Qt Quick'
    }
    if ($Scope -in 'All', 'Native') {
        Use-ObsRuntime
        Invoke-Checks $nativeBuild 'OBS integration'
    }
    if ($Scope -eq 'Desktop') {
        Use-ObsRuntime
        $renderer = Join-Path $project 'dist/obs-sentinel/ui/lookout-ui.exe'
        $builtRenderer = Join-Path $quickBuild "$configuration/lookout-ui.exe"
        if (!(Test-Path -LiteralPath $renderer) -or !(Test-Path -LiteralPath $builtRenderer) -or
            (Get-FileHash -LiteralPath $renderer).Hash -ne (Get-FileHash -LiteralPath $builtRenderer).Hash) {
            throw 'Package the current renderer with installer/package.ps1 before running desktop QA.'
        }
        $cache = Get-BuildCache $nativeBuild
        & $cache.CMAKE_COMMAND --build $nativeBuild --config $configuration --parallel 1 --target sentinel-startup-tests
        Check-Exit 'Desktop harness build'
        $evidence = Join-Path $nativeBuild ('desktop-qa/' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
        New-Item -ItemType Directory -Path $evidence -Force | Out-Null
        $env:SENTINEL_QUICK_QA = $renderer
        $env:SENTINEL_GPU_CAPTURE = Join-Path $evidence 'overlay'
        $env:SENTINEL_BLUR_CAPTURE = Join-Path $evidence 'blur'
        $env:SENTINEL_WGC_QA = $env:SENTINEL_REPLAY_QA = '1'
        $env:SENTINEL_OBS_ROOT = (Get-Item -LiteralPath $ObsBin).Parent.Parent.FullName
        Push-Location -LiteralPath $ObsBin
        try {
            & (Join-Path $nativeBuild "$configuration/sentinel-startup-tests.exe") -platform windows 2>&1 |
                Tee-Object -FilePath (Join-Path $evidence 'desktop.log')
            Check-Exit 'Windows desktop QA'
        }
        finally { Pop-Location }
    }
}
finally {
    foreach ($name in $previous.Keys) { [Environment]::SetEnvironmentVariable($name, $previous[$name]) }
}
