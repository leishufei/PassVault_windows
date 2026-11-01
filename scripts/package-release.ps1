param(
    [string]$BuildDirectory = "$PSScriptRoot/../build/mingw-x64-release",
    [string]$OutputDirectory = "$PSScriptRoot/../build/packages/$(Get-Date -Format 'yyyyMMdd-HHmmss')"
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
$oauthHeader = Join-Path $buildRoot 'generated/passvault/oauth/google_oauth_config.h'
if (!(Test-Path -LiteralPath $oauthHeader)) {
    throw 'Configure and build the Release preset first.'
}
$oauthConfig = Get-Content -Raw -LiteralPath $oauthHeader
foreach ($field in @('kGoogleDesktopClientId', 'kGoogleDesktopClientSecret')) {
    if ($oauthConfig -notmatch ($field + '\s*=\s*"";')) {
        throw 'Public packages require empty OAuth credentials. Reconfigure and rebuild first.'
    }
}
foreach ($tool in @("$env:QT_ROOT/bin/windeployqt.exe", "$env:QT_ROOT/bin/qmake.exe")) {
    if (!(Test-Path -LiteralPath $tool)) { throw 'Set QT_ROOT to the Qt installation.' }
}
if (!(Test-Path -LiteralPath "$env:MINGW_ROOT/licenses")) {
    throw 'Set MINGW_ROOT to the matching MinGW installation with licenses.'
}
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $outputRoot) {
    throw 'Output directory already exists. Choose a fresh directory.'
}
$stage = Join-Path $outputRoot 'PassVault-windows-x64'
$licenses = Join-Path $stage 'licenses'
New-Item -ItemType Directory -Path $licenses -Force | Out-Null
Copy-Item -LiteralPath "$buildRoot/PassVault.exe" -Destination $stage

& "$env:QT_ROOT/bin/windeployqt.exe" --release --compiler-runtime --no-translations `
    --no-opengl-sw --no-system-d3d-compiler --dir $stage "$stage/PassVault.exe"
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed.' }

$vcpkgRoot = Join-Path $buildRoot 'vcpkg_installed/x64-mingw-dynamic'
foreach ($name in @('libcrypto-3-x64.dll', 'libssl-3-x64.dll')) {
    Copy-Item -LiteralPath "$vcpkgRoot/bin/$name" -Destination $stage
}
foreach ($name in @('libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll')) {
    Copy-Item -LiteralPath "$env:MINGW_ROOT/bin/$name" -Destination $stage
}
Copy-Item -LiteralPath "$repoRoot/LICENSE" -Destination $stage
Copy-Item -LiteralPath "$repoRoot/README.md" -Destination $stage
New-Item -ItemType Directory -Path "$stage/resources/screenshots" -Force | Out-Null
Copy-Item -LiteralPath "$repoRoot/resources/screenshots/demo.png" -Destination "$stage/resources/screenshots"
Copy-Item -Path "$repoRoot/resources/fonts/LICENSE-*.txt" -Destination $licenses
Copy-Item -LiteralPath "$vcpkgRoot/share/openssl/copyright" -Destination "$licenses/OpenSSL.txt"
foreach ($component in @('gcc', 'mingw-w64', 'winpthreads')) {
    Copy-Item -LiteralPath "$env:MINGW_ROOT/licenses/$component" -Destination $licenses -Recurse
}
$qtVersion = (& "$env:QT_ROOT/bin/qmake.exe" -query QT_VERSION).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot determine Qt version.' }
foreach ($name in @('LGPL-3.0-only.txt', 'GPL-3.0-only.txt')) {
    Invoke-WebRequest -Uri "https://raw.githubusercontent.com/qt/qtbase/v6.8.3/LICENSES/$name" `
        -OutFile "$licenses/$name"
}
foreach ($module in @('qtbase', 'qtsvg')) {
    $notices = @(Get-ChildItem -Path "$env:QT_ROOT/sbom/$module-$qtVersion.spdx*" -File)
    if ($notices.Count -eq 0) { throw "Missing Qt component notices: $module" }
    $notices | Copy-Item -Destination $licenses
}
@"
PassVault uses dynamically linked Qt $qtVersion, OpenSSL and MinGW runtimes,
and embeds SQLite, Inter, DM Mono and Noto Sans SC.
Project source: https://github.com/leishufei/PassVault_windows
Qt licensing: https://doc.qt.io/qt-6/licensing.html
Qt source: https://download.qt.io/archive/qt/
OpenSSL source: https://www.openssl.org/source/
GCC source: https://gcc.gnu.org/releases.html
MinGW-w64 source: https://www.mingw-w64.org/downloads/
SQLite source (public domain): https://www.sqlite.org/copyright.html
See licenses/ for license texts and Qt component copyright notices (SPDX).
This package contains no vault or OAuth credentials. Google Drive sync requires
a separate build configured with your own OAuth client.
"@ | Set-Content -LiteralPath "$stage/THIRD_PARTY_NOTICES.txt" -Encoding utf8

$archive = Join-Path $outputRoot 'PassVault-windows-x64.zip'
Compress-Archive -LiteralPath $stage -DestinationPath $archive
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $([IO.Path]::GetFileName($archive))" | Set-Content -LiteralPath "$archive.sha256" -Encoding ascii
Write-Output $archive
