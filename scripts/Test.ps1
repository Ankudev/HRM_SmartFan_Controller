param([switch]$AddressSanitizer)
. "$PSScriptRoot/Environment.ps1"
Set-Location $Root
$zig = Join-Path $Local "zig-x86_64-windows-$($Versions.zig)/zig.exe"
$python = Join-Path $Local venv/Scripts/python.exe
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $Local zig-global-cache
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $Local zig-local-cache
$build = Join-Path $Root out/host
$log = Join-Path $Root evidence/test-host.log
New-Item -Force -ItemType Directory $build | Out-Null
$sanitizer = if ($AddressSanitizer) { 'address,undefined' } else { 'undefined' }
"zig c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -fsanitize=$sanitizer" | Set-Content $log -Encoding utf8
Invoke-Checked $zig @('version') | Tee-Object -FilePath $log -Append
Invoke-Checked $python @('--version') | Tee-Object -FilePath $log -Append
$exe = Join-Path $build core_test.exe
Invoke-Checked $zig @('c++', '-std=c++20', '-O1', '-g', '-Wall', '-Wextra', '-Wpedantic',
    '-Werror', "-fsanitize=$sanitizer", '-fno-sanitize-recover=all', '-Ifirmware/SmartFan/src',
    'tests/core_test.cpp', 'tests/radio_test.cpp', 'firmware/SmartFan/src/Core.cpp',
    'firmware/SmartFan/src/Phase.cpp', 'firmware/SmartFan/src/Protocol.cpp',
    'firmware/SmartFan/src/RadioLogic.cpp', '-o', $exe) | Tee-Object -FilePath $log -Append
Invoke-Checked $exe @() | Tee-Object -FilePath $log -Append
$env:SMARTFAN_TEST_EXE = $exe
Invoke-Checked $python @('-m', 'unittest', 'discover', '-s', 'tests', '-v') |
    Tee-Object -FilePath $log -Append
Invoke-Checked $python @('-m', 'pip', 'check') | Tee-Object -FilePath $log -Append
