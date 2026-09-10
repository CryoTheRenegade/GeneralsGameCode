$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

$game = $env:BENCHMARK_GAME
$arch = $env:BENCHMARK_ARCH
$resultDir = 'build/benchmark-results'
New-Item -ItemType Directory -Force -Path $resultDir | Out-Null

$compiler = (Get-Command cl.exe).Source
$expectedHost = if ($arch -eq 'amd64_x86') { 'Hostx64' } else { 'Hostx86' }
if ($compiler -notlike "*\$expectedHost\x86\cl.exe") {
    throw "Unexpected compiler for ${arch}: $compiler"
}

$result = [ordered]@{
    commit = $env:GITHUB_SHA
    game = $game
    arch = $arch
    sample = [int]$env:BENCHMARK_SAMPLE
    runnerImage = $env:ImageVersion
    cpu = @(Get-CimInstance Win32_Processor | Select-Object Name, NumberOfCores, NumberOfLogicalProcessors)
    logicalProcessors = [Environment]::ProcessorCount
    memoryBytes = (Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory
    compiler = $compiler
    compilerVersion = (Get-Item $compiler).VersionInfo.FileVersion
    toolset = $env:VCToolsVersion
    sdk = $env:WindowsSDKVersion
    cmake = (cmake --version | Select-Object -First 1)
    ninja = (ninja --version)
    parallelism = 'Ninja default, matching existing CI'
    configureSeconds = $null
    buildSeconds = $null
    configureExitCode = $null
    buildExitCode = $null
    machine = $null
}

try {
    $gamePrefix = if ($game -eq 'Generals') { 'GENERALS' } else { 'ZEROHOUR' }
    $generals = if ($game -eq 'Generals') { 'ON' } else { 'OFF' }
    $zerohour = if ($game -eq 'GeneralsMD') { 'ON' } else { 'OFF' }
    $flags = @(
        "-DRTS_BUILD_GENERALS=$generals",
        "-DRTS_BUILD_ZEROHOUR=$zerohour",
        '-DRTS_BUILD_CORE_TOOLS=ON',
        '-DRTS_BUILD_CORE_EXTRAS=ON',
        "-DRTS_BUILD_${gamePrefix}_TOOLS=ON",
        "-DRTS_BUILD_${gamePrefix}_EXTRAS=ON"
    )

    # Each job starts on a fresh runner without restored build or dependency caches.
    # Downloads and configuration are timed separately from the clean build.
    $timer = [Diagnostics.Stopwatch]::StartNew()
    cmake --preset win32 @flags *> "$resultDir/configure.log"
    $result.configureExitCode = $LASTEXITCODE
    $timer.Stop()
    $result.configureSeconds = $timer.Elapsed.TotalSeconds
    Get-Content "$resultDir/configure.log"
    if ($result.configureExitCode -ne 0) {
        throw "Configure failed with exit code $($result.configureExitCode)"
    }

    $timer.Restart()
    cmake --build --preset win32 *> "$resultDir/build.log"
    $result.buildExitCode = $LASTEXITCODE
    $timer.Stop()
    $result.buildSeconds = $timer.Elapsed.TotalSeconds
    Get-Content "$resultDir/build.log" -Tail 40
    if ($result.buildExitCode -ne 0) {
        throw "Build failed with exit code $($result.buildExitCode)"
    }

    $exeName = if ($game -eq 'Generals') { 'generalsv.exe' } else { 'generalszh.exe' }
    $exe = "build/win32/$game/Release/$exeName"
    $binary = [IO.File]::ReadAllBytes((Resolve-Path $exe))
    $peOffset = [BitConverter]::ToInt32($binary, 0x3c)
    $machine = [BitConverter]::ToUInt16($binary, $peOffset + 4)
    $result.machine = '0x{0:X4}' -f $machine
    if ($machine -ne 0x014c) {
        throw "Expected an x86 executable, got $($result.machine)"
    }
}
finally {
    $result | ConvertTo-Json -Depth 5 | Set-Content "$resultDir/result.json"
    foreach ($file in @('CMakeCache.txt', 'compile_commands.json')) {
        if (Test-Path "build/win32/$file") {
            Copy-Item "build/win32/$file" "$resultDir/$file"
        }
    }
    if (Test-Path 'build/win32/.ninja_log') {
        Copy-Item 'build/win32/.ninja_log' "$resultDir/ninja-log.txt"
    }
    $result | ConvertTo-Json -Depth 5 | Write-Output
    @"
| Game | Compiler host | Sample | Configure seconds | Build seconds | Build exit | PE machine |
| --- | --- | --- | --- | --- | --- | --- |
| $game | $arch | $env:BENCHMARK_SAMPLE | $($result.configureSeconds) | $($result.buildSeconds) | $($result.buildExitCode) | $($result.machine) |
"@ | Add-Content $env:GITHUB_STEP_SUMMARY
}
