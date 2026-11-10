. "$PSScriptRoot/Environment.ps1"
$actual = Invoke-Arduino version
if ($actual -notmatch "Version: $([regex]::Escape($Versions.arduinoCli))\s") { throw "Wymagany Arduino CLI $($Versions.arduinoCli)" }
$actual
Invoke-Arduino config dump
Invoke-Arduino core update-index
Invoke-Arduino core install $Versions.core
foreach ($library in $Versions.libraries) { Invoke-Arduino lib install $library }
Invoke-Arduino core list
Invoke-Arduino lib list
