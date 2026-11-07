param(
    [string] $Package = (Join-Path $PSScriptRoot 'payload.zip'),
    [switch] $VerifyOnly,
    [string] $Result = (Join-Path $PSScriptRoot 'result.txt')
)
$env:PSModulePath = Join-Path $PSHOME 'Modules'
$ErrorActionPreference = 'Stop'
$token = [Guid]::NewGuid().ToString('N')
$temp = Join-Path ([IO.Path]::GetTempPath()) ('lookout-payload-' + $token)
$pluginRoot = [IO.Path]::GetFullPath((Join-Path $env:ProgramData 'obs-studio\plugins'))
$target = Join-Path $pluginRoot 'lookout'
$previous = Join-Path $pluginRoot 'obs-sentinel'
$staging = Join-Path $pluginRoot ('.lookout-staging-' + $token)
$backup = Join-Path $pluginRoot ('.lookout-backup-' + $token)
$previousBackup = Join-Path $pluginRoot ('.lookout-previous-' + $token)
$installMutex = $null
$hasInstallLock = $false
$exitCode = 0
function Remove-OwnedDirectory([string] $Path, [string] $Parent, [string] $ExpectedName) {
    if (!(Test-Path -LiteralPath $Path)) { return }
    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $parentResolved = (Resolve-Path -LiteralPath $Parent).Path.TrimEnd('\')
    if ((Split-Path $resolved -Parent).TrimEnd('\') -ne $parentResolved -or (Split-Path $resolved -Leaf) -ne $ExpectedName) {
        throw 'Refusing to clean up a directory outside the installer workspace.'
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
function Test-Payload([string] $Directory) {
    $root = [IO.Path]::GetFullPath($Directory).TrimEnd('\') + '\'
    $files = Get-Content -LiteralPath (Join-Path $root 'FILELIST.json') -Raw | ConvertFrom-Json
    if ($files.Count -lt 15) { throw 'The lookout package is incomplete.' }
    foreach ($entry in $files) {
        $path = [IO.Path]::GetFullPath((Join-Path $root $entry.path))
        if (!$path.StartsWith($root, [StringComparison]::OrdinalIgnoreCase) -or !(Test-Path -LiteralPath $path -PathType Leaf)) { throw 'A package file is missing or invalid.' }
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.sha256) { throw "Package verification failed: $($entry.path)" }
    }
    foreach ($required in 'lookout.dll','ui\lookout-ui.exe','ui\Qt6Quick.dll','ui\qml\QtQuick\qmldir','ui\Qt6QuickEffects.dll','ui\qml\QtQuick\Effects\qmldir','ui\platforms\qwindows.dll') {
        if (!(Test-Path -LiteralPath (Join-Path $root $required) -PathType Leaf)) { throw "Required file missing: $required" }
    }
}
function Test-ExistingLookout([string] $Directory, [string] $ModuleFile) {
    $item = Get-Item -LiteralPath $Directory
    if (!$item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
        !(Test-Path -LiteralPath (Join-Path $Directory $ModuleFile) -PathType Leaf) -or
        !(Test-Path -LiteralPath (Join-Path $Directory 'ui\lookout-ui.exe') -PathType Leaf)) {
        throw 'An existing plugin folder could not be identified as lookout. Installation stopped before replacing it.'
    }
}
try {
    if (!$VerifyOnly -and (Get-Process obs64 -ErrorAction SilentlyContinue)) { throw 'Close OBS after stopping any active recording, stream, or replay buffer, then run the installer again.' }
    Expand-Archive -LiteralPath $Package -DestinationPath $temp
    $payload = Join-Path $temp 'lookout'
    Test-Payload $payload
    if ($VerifyOnly) {
        Set-Content -LiteralPath $Result -Value 'lookout package verified.' -Encoding UTF8
    } else {
        $installMutex = [Threading.Mutex]::new($false, 'Global\lookout-plugin-install')
        try { $hasInstallLock = $installMutex.WaitOne(0) }
        catch [Threading.AbandonedMutexException] { $hasInstallLock = $true }
        if (!$hasInstallLock) { throw 'Another lookout installation is in progress.' }
        New-Item -Path $pluginRoot -ItemType Directory -Force | Out-Null
        Copy-Item -LiteralPath $payload -Destination $staging -Recurse
        Test-Payload $staging
        if (Get-Process obs64 -ErrorAction SilentlyContinue) { throw 'OBS opened during installation. Close OBS, then run the installer again.' }
        $replaceCurrent = Test-Path -LiteralPath $target
        $replacePrevious = Test-Path -LiteralPath $previous
        if ($replaceCurrent) { Test-ExistingLookout $target 'lookout.dll' }
        if ($replacePrevious) { Test-ExistingLookout $previous 'obs-sentinel.dll' }
        $moved = $movedPrevious = $false
        try {
            if ($replaceCurrent) { Move-Item -LiteralPath $target -Destination $backup; $moved = $true }
            if ($replacePrevious) { Move-Item -LiteralPath $previous -Destination $previousBackup; $movedPrevious = $true }
            Move-Item -LiteralPath $staging -Destination $target
        } catch {
            if ($movedPrevious) { Move-Item -LiteralPath $previousBackup -Destination $previous }
            if ($moved -and !(Test-Path -LiteralPath $target)) { Move-Item -LiteralPath $backup -Destination $target }
            throw
        }
        if ($moved) { Remove-OwnedDirectory $backup $pluginRoot ('.lookout-backup-' + $token) }
        if ($movedPrevious) { Remove-OwnedDirectory $previousBackup $pluginRoot ('.lookout-previous-' + $token) }
        Set-Content -LiteralPath $Result -Value 'lookout installed. Open OBS and select Docks > lookout. Your existing lookout settings are preserved.' -Encoding UTF8
    }
} catch {
    $exitCode = if ($_.Exception -is [UnauthorizedAccessException] -or $_.CategoryInfo.Category -eq 'PermissionDenied') { 5 } else { 1 }
    Set-Content -LiteralPath $Result -Value $_.Exception.Message -Encoding UTF8
} finally {
    if ($hasInstallLock) { $installMutex.ReleaseMutex() }
    if ($installMutex) { $installMutex.Dispose() }
    Remove-OwnedDirectory $temp ([IO.Path]::GetTempPath()) ('lookout-payload-' + $token)
    Remove-OwnedDirectory $staging $pluginRoot ('.lookout-staging-' + $token)
}
exit $exitCode
