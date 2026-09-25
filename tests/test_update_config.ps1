$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\tools\update_config.ps1')
$setOriginal = "[Chainloader]`r`nScanPath=mods`r`nEnabled=1`r`n`r`n[LoadOrder]`r`nMod1=RoomCatList\RoomCatList.dll`r`nMod2=FuckOffSteve\FuckOffSteve.dll`r`n"
$setUpdated = Add-ItemSetInfoLoadEntry $setOriginal
if (-not $setUpdated.Contains('Mod3=ItemSetInfo\ItemSetInfo.dll')) { throw 'New entry missing.' }
if (-not $setUpdated.StartsWith($setOriginal.TrimEnd())) { throw 'Existing configuration changed.' }
if ((Add-ItemSetInfoLoadEntry $setUpdated) -cne $setUpdated) { throw 'Repeated update was not idempotent.' }
$setFollowing = $setOriginal + "[Other]`r`nKeep=yes`r`n"
$setWithFollowing = Add-ItemSetInfoLoadEntry $setFollowing
if ($setWithFollowing.IndexOf('Mod3=') -gt $setWithFollowing.IndexOf('[Other]')) { throw 'Wrong section.' }
if (-not $setWithFollowing.Contains("[Other]`r`nKeep=yes")) { throw 'Other section changed.' }
$setMissing = Add-ItemSetInfoLoadEntry "[Chainloader]`nScanPath=mods`n"
if (-not $setMissing.Contains('Mod1=ItemSetInfo\ItemSetInfo.dll')) { throw 'Missing section not added.' }
$setRejected = $false
try { Add-ItemSetInfoLoadEntry "[LoadOrder]`nMod2=other.dll`n" | Out-Null } catch { $setRejected = $true }
if (-not $setRejected) { throw 'Numbering gap was silently accepted.' }
'Loader config tests passed: preservation, new entry, idempotence, following sections and numbering-gap rejection.'
