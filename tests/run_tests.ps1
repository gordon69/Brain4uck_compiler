param(
    [string]$CompilerPath = "..\x64\Release\Compiler_x64.exe"
)

$ErrorActionPreference = "Stop"

function Normalize-Newlines([string]$text) {
    return $text.Replace("`r`n", "`n").Replace("`r", "`n")
}

function Assert-Equal([string]$Actual, [string]$Expected, [string]$Message) {
    if ($Actual -ne $Expected) {
        throw "$Message`nExpected: [$Expected]`nActual:   [$Actual]"
    }
}

function Invoke-Compile([string]$Compiler, [string]$Source, [string[]]$CompilerArgs) {
    & $Compiler $Source @CompilerArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Compiler failed for $Source with args: $($CompilerArgs -join ' ')"
    }
}

function Run-ExeCapture([string]$ExePath, [string]$OutputPath, [string]$InputText = $null) {
    if ($null -eq $InputText) {
        & $ExePath | Out-File -FilePath $OutputPath -Encoding ascii -NoNewline
    } else {
        $InputText | & $ExePath | Out-File -FilePath $OutputPath -Encoding ascii -NoNewline
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Executable returned non-zero exit code: $ExePath"
    }
    return [System.IO.File]::ReadAllText($OutputPath)
}

$scriptDir = Split-Path -Parent $PSCommandPath
$rootDir = (Resolve-Path (Join-Path $scriptDir "..")).Path
$casesDir = Join-Path $scriptDir "cases"
$outDir = Join-Path $scriptDir "out"

$compilerFullPath = (Resolve-Path (Join-Path $scriptDir $CompilerPath)).Path
if (-not (Test-Path $compilerFullPath)) {
    throw "Compiler executable not found: $compilerFullPath"
}

if (Test-Path $outDir) {
    Remove-Item $outDir -Recurse -Force
}
New-Item -ItemType Directory -Path $outDir | Out-Null

Write-Host "[1/7] ASM generation test..."
$asmPath = Join-Path $outDir "ab_x64.asm"
Invoke-Compile $compilerFullPath (Join-Path $casesDir "ab_loop.bf") @("--emit", "asm", "--arch", "x64", "--dialect", "nasm", "-o", $asmPath)
if (-not (Test-Path $asmPath)) {
    $fallbackAsm = Join-Path $casesDir "ab_loop.asm"
    if (Test-Path $fallbackAsm) {
        throw "ASM output created at unexpected path: $fallbackAsm (expected: $asmPath)."
    }
    throw "ASM output file was not created."
}
$asmText = [System.IO.File]::ReadAllText($asmPath)
if (($asmText -notmatch "section \.text") -or ($asmText -notmatch "main")) {
    throw "ASM output does not contain expected text section or entry label."
}

Write-Host "[2/7] x64 exe test (loop + output)..."
$exe64Path = Join-Path $outDir "ab_x64.exe"
$stdout64 = Join-Path $outDir "ab_x64.out"
Invoke-Compile $compilerFullPath (Join-Path $casesDir "ab_loop.bf") @("--emit", "exe", "--arch", "x64", "-o", $exe64Path)
$actual64 = Run-ExeCapture $exe64Path $stdout64
Assert-Equal $actual64 "AB" "x64 AB test failed."

Write-Host "[3/7] x86 exe test (loop + output)..."
$exe86Path = Join-Path $outDir "ab_x86.exe"
$stdout86 = Join-Path $outDir "ab_x86.out"
Invoke-Compile $compilerFullPath (Join-Path $casesDir "ab_loop.bf") @("--emit", "exe", "--arch", "x86", "-o", $exe86Path)
$actual86 = Run-ExeCapture $exe86Path $stdout86
Assert-Equal $actual86 "AB" "x86 AB test failed."

Write-Host "[4/7] hello world test (x64)..."
$helloExe = Join-Path $outDir "hello_x64.exe"
$helloOut = Join-Path $outDir "hello_x64.out"
Invoke-Compile $compilerFullPath (Join-Path $casesDir "hello_world.bf") @("--emit", "exe", "--arch", "x64", "-o", $helloExe)
$helloActual = Normalize-Newlines (Run-ExeCapture $helloExe $helloOut)
Assert-Equal $helloActual "Hello World!" "Hello world x64 test failed."

Write-Host "[5/7] hello world test (x86)..."
$helloExeX86 = Join-Path $outDir "hello_x86.exe"
$helloOutX86 = Join-Path $outDir "hello_x86.out"
Invoke-Compile $compilerFullPath (Join-Path $casesDir "hello_world.bf") @("--emit", "exe", "--arch", "x86", "-o", $helloExeX86)
$helloActualX86 = Normalize-Newlines (Run-ExeCapture $helloExeX86 $helloOutX86)
Assert-Equal $helloActualX86 "Hello World!" "Hello world x86 test failed."

Write-Host "[6/7] input echo test (x64)..."
$echoExe = Join-Path $outDir "echo_x64.exe"
$echoOut = Join-Path $outDir "echo_x64.out"
Invoke-Compile $compilerFullPath (Join-Path $casesDir "echo.bf") @("--emit", "exe", "--arch", "x64", "-o", $echoExe)
$echoActual = Run-ExeCapture $echoExe $echoOut "Z"
Assert-Equal $echoActual "Z" "Echo x64 test failed."

Write-Host "[7/7] input echo test (x86)..."
$echoExeX86 = Join-Path $outDir "echo_x86.exe"
$echoOutX86 = Join-Path $outDir "echo_x86.out"
Invoke-Compile $compilerFullPath (Join-Path $casesDir "echo.bf") @("--emit", "exe", "--arch", "x86", "-o", $echoExeX86)
$echoActualX86 = Run-ExeCapture $echoExeX86 $echoOutX86 "Z"
Assert-Equal $echoActualX86 "Z" "Echo x86 test failed."

Write-Host "All tests passed."
