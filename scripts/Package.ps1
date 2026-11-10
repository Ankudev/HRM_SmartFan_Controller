$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$root = (Get-Location).Path
$profiles = @('dry-run', 'ac-hardware')
function Verify-HashList([string]$List, [string]$Base) {
    foreach ($line in (Get-Content -LiteralPath $List)) {
        if ($line -notmatch '^([a-f0-9]{64})  (.+)$') { throw "Niepoprawna linia $List" }
        $expected = $Matches[1]
        $file = Join-Path $Base $Matches[2]
        if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $expected) {
            throw "Niezgodna suma SHA-256 $file. Uruchom Build.ps1."
        }
    }
}
foreach ($profile in $profiles) {
    Verify-HashList "out/$profile/SOURCE-SHA256.txt" $root
    Verify-HashList "out/$profile/ARTIFACT-SHA256.txt" "out/$profile/build"
}

$inputs = @(
    Get-Item .gitattributes, .gitignore, README.md, versions.json
    Get-ChildItem client, docs, evidence, firmware, scripts, tests -File -Recurse |
        Where-Object { $_.FullName -notmatch '[\\/]__pycache__[\\/]' }
)
$projectHashes = foreach ($file in ($inputs | Sort-Object FullName)) {
    $relative = [IO.Path]::GetRelativePath($root, $file.FullName).Replace('\','/')
    "$((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant())  $relative"
}
$projectHashes | Set-Content out/PROJECT-SHA256.txt -Encoding utf8
$inputs += Get-Item out/PROJECT-SHA256.txt
foreach ($profile in $profiles) {
    $inputs += Get-Item "out/$profile/SOURCE-SHA256.txt", "out/$profile/ARTIFACT-SHA256.txt"
    $inputs += Get-ChildItem "out/$profile/build" -File |
        Where-Object { $_.Name -match '^SmartFan\.ino\.(.*\.bin|bin|elf|map)$' }
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = Join-Path $root out/HRM_SmartFan_v2.zip
$temp = "$archive.$([guid]::NewGuid().ToString('N')).tmp"
try {
    $zip = [IO.Compression.ZipFile]::Open($temp, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($file in $inputs) {
            $entry = [IO.Path]::GetRelativePath($root, $file.FullName).Replace('\','/')
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                $zip, $file.FullName, $entry, [IO.Compression.CompressionLevel]::Optimal) | Out-Null
        }
    } finally { $zip.Dispose() }
    Move-Item -Force -LiteralPath $temp -Destination $archive
} finally {
    if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp }
}
"$((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant())  HRM_SmartFan_v2.zip" |
    Set-Content out/PACKAGE-SHA256.txt -Encoding ascii
Get-Item -LiteralPath $archive
