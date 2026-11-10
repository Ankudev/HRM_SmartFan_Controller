$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/Environment.ps1"
Set-Location $Root
$tool = Join-Path $env:ARDUINO_DIRECTORIES_DATA "packages/esp32/tools/esp-x32/$($Versions.espToolchain)/bin/xtensa-esp32s3-elf-objdump.exe"
$elf = 'out/ac-hardware/build/SmartFan.ino.elf'
$dryElf = 'out/dry-run/build/SmartFan.ino.elf'
$log = Join-Path $Root evidence/isr-elf.log
"xtensa-esp32s3-elf-objdump -t -C i -d -C --section=.iram0.text $elf" | Set-Content $log -Encoding utf8
$symbols = @(Invoke-Checked $tool @('-t', '-C', $elf))
$drySymbols = @(Invoke-Checked $tool @('-t', '-C', $dryElf))
$iramNames = @('onZeroCross\(', 'onTimer\(', 'Phase::edge\(', 'Phase::alarm\(', 'Phase::timingFault\(',
    'gpio_intr_service$', 'xPortEnterCriticalTimeout$', 'vPortExitCritical$')
foreach ($pattern in $iramNames) {
    $match = $symbols | Where-Object { $_ -match "\.iram0\.text.*$pattern" }
    if (!$match) { throw "Brak funkcji w IRAM: $pattern" }
    $match | Tee-Object -FilePath $log -Append
}
foreach ($pattern in @('phaseLock$', '\)::phase$')) {
    $match = $symbols | Where-Object { $_ -match "\.dram0\.data.*$pattern" }
    if (!$match) { throw "Brak obiektu w DRAM: $pattern" }
    $match | Tee-Object -FilePath $log -Append
}
if ($drySymbols -match 'onZeroCross\(|\)::onTimer\(|fan::Phase::alarm\(') {
    throw 'Ścieżka AC jest obecna w ELF dry-run.'
}
'Brak funkcji wyzwalania AC w ELF dry-run.' | Tee-Object -FilePath $log -Append

$iramFunctions = @{}
foreach ($line in $symbols) {
    if ($line -match '^([a-f0-9]+)\s.*\bF\s+\.iram0\.text\s') { $iramFunctions[$Matches[1]] = $true }
}
$scanned = 0
foreach ($symbol in $symbols) {
    if ($symbol -notmatch '^([a-f0-9]+)\s.*\bF\s+\.iram0\.text\s+([a-f0-9]+)\s+(.+)$') { continue }
    $start = $Matches[1]
    $size = $Matches[2]
    $name = $Matches[3]
    if ($name -notmatch '^\(anonymous namespace\)::on(ZeroCross|Timer)\(|^fan::Phase::(edge|alarm|leaseValid|cancel|timingFault)\(') { continue }
    ++$scanned
    $stop = ([Convert]::ToInt64($start, 16) + [Convert]::ToInt64($size, 16)).ToString('x')
    $symbol | Add-Content $log -Encoding utf8
    $assembly = Invoke-Checked $tool @('-d', '-C', '--section=.iram0.text',
        "--start-address=0x$start", "--stop-address=0x$stop", $elf)
    foreach ($line in $assembly) {
        if ($line -match '\b(call[04812]+|l32r)\s') {
            $line | Add-Content $log -Encoding utf8
            if ($line -match '\bcall[04812]+\s+([a-f0-9]+)\s' -and !$iramFunctions.ContainsKey($Matches[1])) {
                throw "Wywołanie poza IRAM w ścieżce AC: $line"
            }
            if ($line -match '\bl32r\s+\w+,\s+([a-f0-9]+)\s' -and $Matches[1] -notmatch '^403[0-9a-f]{5}$') {
                throw "Literał poza obszarem IRAM: $line"
            }
        } elseif ($line -match '\bcallx[0-9]+\s') {
            throw "Wywołanie pośrednie w ścieżce AC: $line"
        }
    }
}
if ($scanned -lt 5) { throw 'Nie udało się zidentyfikować funkcji do analizy.' }
"Sprawdzono $scanned funkcji ścieżki AC." | Tee-Object -FilePath $log -Append
