function Add-ItemSetInfoLoadEntry {
    param([Parameter(Mandatory=$true)][string]$Content)
    $setEntry = 'ItemSetInfo\ItemSetInfo.dll'
    $setLines = [System.Collections.Generic.List[string]]::new()
    foreach ($setLine in ($Content -split '\r?\n')) { $setLines.Add($setLine) }
    $setStart = -1
    $setEnd = $setLines.Count
    for ($setIndex = 0; $setIndex -lt $setLines.Count; ++$setIndex) {
        if ($setLines[$setIndex] -match '^\s*\[LoadOrder\]\s*$') {
            if ($setStart -ge 0) { throw 'Duplicate LoadOrder sections.' }
            $setStart = $setIndex
        } elseif ($setStart -ge 0 -and $setEnd -eq $setLines.Count -and $setLines[$setIndex] -match '^\s*\[') {
            $setEnd = $setIndex
        }
    }
    if ($setStart -lt 0) {
        return $Content.TrimEnd() + "`r`n`r`n[LoadOrder]`r`nMod1=$setEntry`r`n"
    }
    $setNumbers = [System.Collections.Generic.List[int]]::new()
    $setAlreadyRegistered = $false
    for ($setIndex = $setStart + 1; $setIndex -lt $setEnd; ++$setIndex) {
        if ($setLines[$setIndex] -match '^\s*Mod(\d+)\s*=\s*(.*?)\s*$') {
            $setNumber = [int]$Matches[1]
            $setValue = $Matches[2].Replace('/', '\')
            if ($setNumbers.Contains($setNumber)) { throw 'Duplicate Mod number in LoadOrder.' }
            $setNumbers.Add($setNumber)
            if ($setValue -ieq $setEntry) { $setAlreadyRegistered = $true }
        }
    }
    for ($setIndex = 1; $setIndex -le $setNumbers.Count; ++$setIndex) {
        if (-not $setNumbers.Contains($setIndex)) { throw 'LoadOrder has a numbering gap; no configuration changes made.' }
    }
    if ($setAlreadyRegistered) { return $Content }
    $setLines.Insert($setEnd, ('Mod{0}={1}' -f ($setNumbers.Count + 1), $setEntry))
    return (($setLines -join "`r`n").TrimEnd() + "`r`n")
}
