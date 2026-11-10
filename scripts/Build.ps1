param([ValidateSet('both', 'dry-run', 'ac-hardware')][string]$Profile = 'both')
. "$PSScriptRoot/Environment.ps1"
Set-Location $Root

$paths = @(Get-ChildItem firmware -File -Recurse -Include '*.h','*.cpp','*.ino' | Resolve-Path -Relative) + @('versions.json', 'scripts/Build.ps1')
$sourceHashes = foreach ($path in ($paths | Sort-Object)) {
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant()
    "$hash  $($path.Replace('\','/'))"
}
$inputBytes = [Text.Encoding]::UTF8.GetBytes($sourceHashes -join "`n")
$sha = [Security.Cryptography.SHA256]::Create()
try { $sourceId = ([BitConverter]::ToString($sha.ComputeHash($inputBytes))).Replace('-', '').ToLowerInvariant() }
finally { $sha.Dispose() }

# __DATE__/__TIME__ w chip-debug-report.cpp rdzenia psuje powtarzalnosc ELF. GCC bierze je z
# SOURCE_DATE_EPOCH, wiec ustawiam czas ostatniego commita (bez gita: 0, czyli 1 stycznia 1970).
$epoch = if (Get-Command git -ErrorAction SilentlyContinue) { git -C $Root log -1 --format=%ct 2>$null }
$env:SOURCE_DATE_EPOCH = if ("$epoch" -match '^\d+$') { "$epoch" } else { '0' }
# Wlasne flagi ida do compiler.*.extra_flags (puste w platform.txt). Nadpisanie build.extra_flags
# usuwaloby definicje plytki: ESP32, ARDUINO_USB_MODE, ARDUINO_USB_CDC_ON_BOOT, CORE_DEBUG_LEVEL.
$prefixMap = "`"-ffile-prefix-map=$($Root.Replace('\','/'))=.`""

$profiles = if ($Profile -eq 'both') { @('dry-run','ac-hardware') } else { @($Profile) }
foreach ($current in $profiles) {
    $ac = [int]($current -eq 'ac-hardware')
    $id = "$($sourceId.Substring(0,11))-$ac"
    $folder = Join-Path (Join-Path $Root out) $current
    $evidence = Join-Path $Root evidence
    New-Item -Force -ItemType Directory $folder,$evidence | Out-Null
    $build = Join-Path $folder build
    $log = Join-Path $evidence "build-$current.log"
    "Build=$id FQBN=$($Versions.fqbn) SOURCE_DATE_EPOCH=$env:SOURCE_DATE_EPOCH" | Set-Content $log -Encoding utf8
    $sourceHashes | Set-Content (Join-Path $folder SOURCE-SHA256.txt) -Encoding ascii
    $flags = "-DFAN_AC_HARDWARE=$ac -DFAN_BUILD_ID=`"$id`" $prefixMap"
    Invoke-Arduino compile --clean --jobs 6 --warnings default --fqbn $Versions.fqbn --build-path $build `
        --build-property "compiler.cpp.extra_flags=$flags" --build-property "compiler.c.extra_flags=$prefixMap" `
        (Join-Path $Root firmware/SmartFan) |
        Tee-Object -FilePath $log -Append

    $artifacts = Get-ChildItem -File $build | Where-Object { $_.Name -match '^SmartFan\.ino\.(.*\.bin|bin|elf|map)$' }
    if (!(Test-Path (Join-Path $build 'SmartFan.ino.bin'))) { throw "Brak obrazu firmware" }
    $artifacts | ForEach-Object {
        "$((Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash.ToLowerInvariant())  $($_.Name)"
    } | Set-Content (Join-Path $folder ARTIFACT-SHA256.txt) -Encoding ascii
}
