param(
    [Parameter(Mandatory)] [string] $CMakeExe,
    [Parameter(Mandatory)] [string] $QtSdk,
    [Parameter(Mandatory)] [string] $CrtDir,
    [Parameter(Mandatory)] [string] $QtLicenseDir,
    [string] $Version = '1.0.0-rc.1'
)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$dist = Join-Path $project 'dist'
$stage = Join-Path $project 'stage'
$payload = Join-Path $stage 'obs-sentinel'
$ui = Join-Path $payload 'ui'
$encoding = [Text.UTF8Encoding]::new($false)
$QtSdk = (Resolve-Path -LiteralPath $QtSdk).Path
function Remove-PackageDirectory([string] $Directory, [string] $Parent, [string] $Name) {
    if (!(Test-Path -LiteralPath $Directory)) { return }
    $resolved = (Resolve-Path -LiteralPath $Directory).Path
    if ((Split-Path $resolved -Parent) -ne $Parent -or (Split-Path $resolved -Leaf) -ne $Name) { throw 'Unexpected package cleanup target.' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
function Check-Native([string] $Action) { if ($LASTEXITCODE -ne 0) { throw "$Action failed ($LASTEXITCODE)." } }
Remove-PackageDirectory $stage $project 'stage'
New-Item -ItemType Directory -Path $ui,$dist -Force | Out-Null
& $CMakeExe --install (Join-Path $project 'build') --config RelWithDebInfo --prefix $stage
Check-Native 'Plugin staging'
& $CMakeExe --install (Join-Path $project 'build-quick') --config RelWithDebInfo --prefix $stage
Check-Native 'Renderer staging'
& (Join-Path $QtSdk 'bin\windeployqt.exe') --release --qmldir (Join-Path $project 'quick') --no-translations --no-opengl-sw --no-system-d3d-compiler --no-compiler-runtime --skip-plugin-types 'generic,networkinformation,tls,qmltooling' --dir $ui (Join-Path $ui 'lookout-ui.exe')
Check-Native 'Qt runtime staging'
Get-ChildItem -LiteralPath $CrtDir -Filter '*.dll' -File | Copy-Item -Destination $ui
[IO.File]::WriteAllText((Join-Path $ui 'qt.conf'),"[Paths]`nPrefix=.`nPlugins=.`nQmlImports=qml`n",$encoding)
$readme = Join-Path $project 'README.github.md'
if (!(Test-Path -LiteralPath $readme)) { $readme = Join-Path $project 'README.md' }
Copy-Item -LiteralPath $readme -Destination (Join-Path $payload 'README.md')
Copy-Item -LiteralPath (Join-Path $project 'LICENSE'),(Join-Path $PSScriptRoot 'THIRD-PARTY-NOTICES.md') -Destination $payload
New-Item -ItemType Directory -Path (Join-Path $ui 'licenses') | Out-Null
foreach ($name in 'LGPL-3.0-only.txt','GPL-3.0-only.txt','GPL-2.0-only.txt') { Copy-Item -LiteralPath (Join-Path $QtLicenseDir $name) -Destination (Join-Path $ui 'licenses') }
$manifest = [ordered]@{
    version = $Version
    build = '2026-11-07-rc1'
    builtUtc = [DateTime]::UtcNow.ToString('o')
    dllSha256 = (Get-FileHash -LiteralPath (Join-Path $payload 'obs-sentinel.dll')).Hash
    rendererSha256 = (Get-FileHash -LiteralPath (Join-Path $ui 'lookout-ui.exe')).Hash
    renderer = 'Qt Quick 6.8.3, private D3D11 process'
    brandingRevision = 17
}
[IO.File]::WriteAllText((Join-Path $payload 'BUILDINFO.json'),($manifest | ConvertTo-Json),$encoding)
$files = @(Get-ChildItem -LiteralPath $payload -File -Recurse | Sort-Object FullName | ForEach-Object {
    [ordered]@{ path = $_.FullName.Substring($payload.Length+1); sha256 = (Get-FileHash -LiteralPath $_.FullName).Hash }
})
[IO.File]::WriteAllText((Join-Path $payload 'FILELIST.json'),(ConvertTo-Json -InputObject $files),$encoding)
$zip = Join-Path $dist "lookout-$Version-windows-x64.zip"
Compress-Archive -LiteralPath $payload -DestinationPath $zip -Force
$setupBuild = Join-Path $project 'build-installer'
& $CMakeExe -S $PSScriptRoot -B $setupBuild -A x64 "-DLOOKOUT_PAYLOAD=$zip"
Check-Native 'Installer configuration'
& $CMakeExe --build $setupBuild --config Release
Check-Native 'Installer build'
Copy-Item -LiteralPath (Join-Path $setupBuild 'Release\Install-Lookout.exe') -Destination (Join-Path $dist "install-lookout-$Version.exe") -Force
Remove-PackageDirectory (Join-Path $dist 'obs-sentinel') $dist 'obs-sentinel'
Copy-Item -LiteralPath $payload -Destination (Join-Path $dist 'obs-sentinel') -Recurse
Write-Output "Built lookout $Version installer and portable package."
