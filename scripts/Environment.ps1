$ErrorActionPreference = 'Stop'
$Root = Split-Path $PSScriptRoot -Parent
$Versions = Get-Content (Join-Path $Root versions.json) -Raw | ConvertFrom-Json
$Local = Join-Path $Root .local
$env:ARDUINO_DIRECTORIES_DATA = Join-Path $Local arduino-data
$env:ARDUINO_DIRECTORIES_DOWNLOADS = Join-Path $Local arduino-downloads
$env:ARDUINO_DIRECTORIES_USER = Join-Path $Local arduino-user
$env:ARDUINO_BUILD_CACHE_PATH = Join-Path $Local arduino-cache
$env:ARDUINO_BOARD_MANAGER_ADDITIONAL_URLS = $Versions.esp32Index
$ArduinoConfig = Join-Path $Local arduino-cli.yaml
New-Item -Force -ItemType Directory $Local | Out-Null
if (!(Test-Path $ArduinoConfig)) { '{}' | Set-Content $ArduinoConfig -Encoding ascii }
function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments 2>&1 | ForEach-Object { "$_" }
    if ($LASTEXITCODE -ne 0) { throw "$Program zakończył się kodem $LASTEXITCODE" }
}
function Invoke-Arduino {
    $cliPath = Join-Path $Local arduino-cli.exe
    $arduino = if (Test-Path $cliPath) { $cliPath } else { (Get-Command arduino-cli -ErrorAction Stop).Source }
    Invoke-Checked $arduino (@('--config-file', $ArduinoConfig) + $args)
}
