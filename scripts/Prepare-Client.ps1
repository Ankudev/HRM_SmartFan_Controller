$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/Environment.ps1"
Set-Location $Root
Invoke-Checked py.exe @("-$($Versions.python)", '-m', 'venv', (Join-Path $Local venv))
$python = Join-Path $Local venv/Scripts/python.exe
$actual = Invoke-Checked $python @('--version')
if ($actual -ne "Python $($Versions.pythonPatch)") { throw "Wymagany Python $($Versions.pythonPatch)" }
$actual
Invoke-Checked $python @('-m', 'pip', 'install', '-r', 'client/requirements-windows.txt')
Invoke-Checked $python @('-m', 'pip', 'check')
Invoke-Checked $python @('-m', 'pip', 'freeze')
