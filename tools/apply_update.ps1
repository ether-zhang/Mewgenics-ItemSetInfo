$ErrorActionPreference = 'Stop'
$setProjectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$setGameRoot = (Resolve-Path -LiteralPath (Join-Path $setProjectRoot '..\..')).Path
$setSource = Join-Path $setProjectRoot 'build\ItemSetInfo.dll'
$setTarget = Join-Path $setProjectRoot 'ItemSetInfo.dll'
$setConfig = Join-Path $setGameRoot 'chainloader.ini'
function Get-ItemSetSha256 {
    param([string]$Path)
    $setStream = [System.IO.File]::OpenRead($Path)
    $setHasher = [System.Security.Cryptography.SHA256]::Create()
    try { return [System.BitConverter]::ToString($setHasher.ComputeHash($setStream)).Replace('-', '') }
    finally { $setStream.Dispose(); $setHasher.Dispose() }
}
if ((Test-Path -LiteralPath $setTarget) -and @(Get-Process -Name Mewgenics -ErrorAction SilentlyContinue).Count -gt 0) {
    throw 'Please exit Mewgenics normally before installing ItemSetInfo. No game process was stopped.'
}
# A first install adds a DLL that is not in the running process. Mewjector reads
# its configuration once at startup, so this is staged for the next launch.
# Replacing an existing DLL still requires the game to be closed.
if (-not (Test-Path -LiteralPath $setSource)) { throw 'The compiled ItemSetInfo.dll is missing.' }
if (-not (Test-Path -LiteralPath (Join-Path $setGameRoot 'version.dll'))) { throw 'Mewjector is required.' }
if (-not (Test-Path -LiteralPath $setConfig)) { throw 'Existing Mewjector chainloader.ini was not found.' }
. (Join-Path $PSScriptRoot 'update_config.ps1')
$setOriginalConfig = [System.IO.File]::ReadAllText($setConfig)
$setNextConfig = Add-ItemSetInfoLoadEntry $setOriginalConfig
$setSourceHash = Get-ItemSetSha256 $setSource
$setBackup = Join-Path $setProjectRoot ('build\backup-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
if (Test-Path -LiteralPath $setBackup) { throw 'Backup directory already exists.' }
New-Item -ItemType Directory -Path $setBackup | Out-Null
Copy-Item -LiteralPath $setConfig -Destination (Join-Path $setBackup 'chainloader.ini')
if (Test-Path -LiteralPath $setTarget) { Copy-Item -LiteralPath $setTarget -Destination (Join-Path $setBackup 'ItemSetInfo.dll') }
Copy-Item -LiteralPath $setSource -Destination $setTarget
if ((Get-ItemSetSha256 $setTarget) -ne $setSourceHash) { throw 'DLL copy failed verification; loader configuration was not changed.' }
if ($setNextConfig -cne $setOriginalConfig) {
    if ([System.IO.File]::ReadAllText($setConfig) -cne $setOriginalConfig) { throw 'Loader configuration changed during update; registration was not applied.' }
    [System.IO.File]::WriteAllText($setConfig, $setNextConfig, [System.Text.UTF8Encoding]::new($false))
}
if ([System.IO.File]::ReadAllText($setConfig) -cne $setNextConfig) { throw 'Loader configuration failed verification.' }
'ItemSetInfo installed and registered. Existing mods and shared loader were preserved.'
'The new Mod is loaded on the next game launch; the current session is unchanged.'
'DLL SHA256: ' + $setSourceHash
'Backup: ' + $setBackup
