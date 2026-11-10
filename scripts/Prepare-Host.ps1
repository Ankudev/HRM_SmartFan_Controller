$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/Environment.ps1"
Set-Location $Root
$archive = Join-Path $Local "zig-x86_64-windows-$($Versions.zig).zip"
$executable = Join-Path $Local "zig-x86_64-windows-$($Versions.zig)/zig.exe"
if (!(Test-Path $executable)) {
    Invoke-Checked curl.exe @('-fLsS', '--retry', '2',
        "https://ziglang.org/download/$($Versions.zig)/zig-x86_64-windows-$($Versions.zig).zip",
        '-o', $archive)
    if ((Get-FileHash -Algorithm SHA256 $archive).Hash -ne $Versions.zigWindowsX64Sha256) {
        throw 'Suma Zig nie jest zgodna z manifestem.'
    }
    Invoke-Checked tar.exe @('-xf', $archive, '-C', $Local)
}
Invoke-Checked $executable @('version')
& "$PSScriptRoot/Prepare-Client.ps1"
