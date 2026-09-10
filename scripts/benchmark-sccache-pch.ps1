param([ValidateSet('configure', 'cold', 'warm')][string]$Phase)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$game = $env:BENCHMARK_GAME
$mode = $env:BENCHMARK_MODE
$resultDir = 'build/benchmark-results'
New-Item -ItemType Directory -Force -Path $resultDir | Out-Null

if ($Phase -eq 'configure') {
    $prefix = if ($game -eq 'Generals') { 'GENERALS' } else { 'ZEROHOUR' }
    $generals = if ($game -eq 'Generals') { 'ON' } else { 'OFF' }
    $zerohour = if ($game -eq 'GeneralsMD') { 'ON' } else { 'OFF' }
    $flags = @(
        "-DRTS_BUILD_GENERALS=$generals", "-DRTS_BUILD_ZEROHOUR=$zerohour",
        '-DRTS_BUILD_CORE_TOOLS=ON', '-DRTS_BUILD_CORE_EXTRAS=ON',
        "-DRTS_BUILD_${prefix}_TOOLS=ON", "-DRTS_BUILD_${prefix}_EXTRAS=ON"
    )
    if ($mode -ne 'baseline') {
        $flags += '-DCMAKE_C_COMPILER_LAUNCHER=sccache', '-DCMAKE_CXX_COMPILER_LAUNCHER=sccache'
    }
    if ($mode -eq 'textual-cache') {
        $hook = (Resolve-Path 'cmake/benchmark-textual-pch.cmake').Path.Replace('\', '/')
        $flags += "-DCMAKE_PROJECT_INCLUDE=$hook"
    }
    cmake --preset win32 @flags *> "$resultDir/configure.log"
    $code = $LASTEXITCODE
    Get-Content "$resultDir/configure.log"
    if ($code -ne 0) { exit $code }

    $commands = @(Get-Content 'build/win32/compile_commands.json' -Raw | ConvertFrom-Json |
        Where-Object { $_.command -match 'CMAKE_INTDIR=.*Release' })
    $uses = @($commands | Where-Object { $_.command -match ' /Yu' })
    $creates = @($commands | Where-Object { $_.command -match ' /Yc' })
    $forced = @($commands | Where-Object { $_.command -match ' /FI.*cmake_pch' })
    if ($mode -eq 'textual-cache' -and ($uses.Count -ne 0 -or $forced.Count -eq 0)) {
        throw 'Textual header experiment did not preserve forced includes or remove PCH consumption'
    }
    if ($mode -ne 'textual-cache' -and $uses.Count -eq 0) {
        throw 'Expected existing PCH consumption flags'
    }
    [ordered]@{
        game = $game; mode = $mode; commit = $env:GITHUB_SHA
        cpu = @(Get-CimInstance Win32_Processor | Select-Object Name, NumberOfLogicalProcessors)
        runnerImage = $env:ImageVersion
        compiler = (Get-Command cl.exe).Source
        compilerVersion = (Get-Item (Get-Command cl.exe).Source).VersionInfo.FileVersion
        toolset = $env:VCToolsVersion; sdk = $env:WindowsSDKVersion
        sccache = (sccache --version); cmake = (cmake --version | Select-Object -First 1)
        ninja = (ninja --version); compileCommands = $commands.Count
        pchConsumers = $uses.Count; pchCreators = $creates.Count; forcedHeaderCommands = $forced.Count
    } | ConvertTo-Json -Depth 5 | Set-Content "$resultDir/environment.json"
    Copy-Item 'build/win32/compile_commands.json' "$resultDir/compile_commands.json"
    Copy-Item 'build/win32/CMakeCache.txt' "$resultDir/CMakeCache.txt"
    $pchHeaders = @(Get-ChildItem 'build/win32' -Recurse -Filter 'cmake_pch.h*' -File |
        Where-Object { $_.Extension -in @('.h', '.hxx') } |
        ForEach-Object { @{ path = $_.FullName; content = Get-Content $_.FullName -Raw } })
    $pchHeaders | ConvertTo-Json -Depth 3 | Set-Content "$resultDir/pch-headers.json"
    exit 0
}

if ($mode -ne 'baseline') {
    sccache --zero-stats
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
$result = [ordered]@{ game = $game; mode = $mode; phase = $Phase; buildSeconds = $null; exitCode = $null; machine = $null }
try {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    cmake --build --preset win32 *> "$resultDir/$Phase.log"
    $result.exitCode = $LASTEXITCODE
    $timer.Stop()
    $result.buildSeconds = $timer.Elapsed.TotalSeconds
    Get-Content "$resultDir/$Phase.log" -Tail 30
    if ($result.exitCode -ne 0) { throw "Build failed: $($result.exitCode)" }
    $exeName = if ($game -eq 'Generals') { 'generalsv.exe' } else { 'generalszh.exe' }
    $binary = [IO.File]::ReadAllBytes((Resolve-Path "build/win32/$game/Release/$exeName"))
    $offset = [BitConverter]::ToInt32($binary, 0x3c)
    $machine = [BitConverter]::ToUInt16($binary, $offset + 4)
    $result.machine = '0x{0:X4}' -f $machine
    if ($machine -ne 0x014c) { throw 'Expected x86 output' }
}
finally {
    $result | ConvertTo-Json | Set-Content "$resultDir/$Phase.json"
    if ($mode -ne 'baseline') {
        sccache --show-stats --stats-format json > "$resultDir/$Phase-stats.json"
        sccache --show-stats > "$resultDir/$Phase-stats.txt"
        Get-Content "$resultDir/$Phase-stats.txt"
    }
    if (Test-Path 'build/win32/.ninja_log') {
        Copy-Item 'build/win32/.ninja_log' "$resultDir/$Phase-ninja-log.txt"
    }
    $result | ConvertTo-Json | Write-Output
    "$game / $mode / $Phase : $($result.buildSeconds) seconds, exit $($result.exitCode)" | Add-Content $env:GITHUB_STEP_SUMMARY
}
