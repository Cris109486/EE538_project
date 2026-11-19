param(
    [ValidateSet('Configure', 'Build', 'Run', 'Test')]
    [string]$Action = 'Build',
    [ValidateSet('fcfs', 'speed', 'energy')]
    [string]$Policy = 'fcfs',
    [string]$InputFile = 'examples/hand_calculated.json'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path -Parent $PSScriptRoot
$configPath = Join-Path $PSScriptRoot 'toolchain.local.json'
if (-not (Test-Path -LiteralPath $configPath)) {
    throw 'Missing scripts/toolchain.local.json. See docs/local-run.md for its format.'
}
$toolsConfig = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
foreach ($name in @('compiler', 'cmake', 'ninja')) {
    if (-not (Test-Path -LiteralPath $toolsConfig.$name -PathType Leaf)) {
        throw "Tool not found: $name = $($toolsConfig.$name). Update scripts/toolchain.local.json."
    }
}
$compilerBin = Split-Path -Parent $toolsConfig.compiler
$env:PATH = "$compilerBin;$env:PATH" # this process only; no system/user PATH mutation
$buildDir = Join-Path $projectRoot 'build/windows-debug'
$outputDir = Join-Path $projectRoot 'out'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

function Invoke-Checked {
    param([string]$Executable, [string[]]$Arguments)
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Executable exited with code $LASTEXITCODE" }
}

$configureArgs = @('-S', $projectRoot, '-B', $buildDir, '-G', 'Ninja',
    "-DCMAKE_CXX_COMPILER=$($toolsConfig.compiler)", "-DCMAKE_MAKE_PROGRAM=$($toolsConfig.ninja)",
    '-DCMAKE_BUILD_TYPE=Debug', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON')
if ($toolsConfig.PSObject.Properties.Name -contains 'python') {
    if (Test-Path -LiteralPath $toolsConfig.python -PathType Leaf) {
        $configureArgs += "-DPython3_EXECUTABLE=$($toolsConfig.python)"
    }
}
Invoke-Checked -Executable $toolsConfig.cmake -Arguments $configureArgs
if ($Action -eq 'Configure') { exit 0 }
Invoke-Checked -Executable $toolsConfig.cmake -Arguments @('--build', $buildDir, '--parallel', '2')
if ($Action -eq 'Test') {
    $ctestExe = Join-Path (Split-Path -Parent $toolsConfig.cmake) 'ctest.exe'
    Invoke-Checked -Executable $ctestExe -Arguments @('--test-dir', $buildDir, '--output-on-failure')
} elseif ($Action -eq 'Run') {
    if (-not [System.IO.Path]::IsPathRooted($InputFile)) { $InputFile = Join-Path $projectRoot $InputFile }
    $resultFile = Join-Path $outputDir "$Policy.json"
    $programExe = Join-Path $buildDir 'ee538_scheduler.exe'
    Invoke-Checked -Executable $programExe -Arguments @($InputFile, $Policy, $resultFile)
    Write-Host "Result: $resultFile"
    (Get-Content -LiteralPath $resultFile -Raw | ConvertFrom-Json).summary | Format-List
}
