# Runs the slices of the SynapseMFT suite that have executable tests.
# Protocol tests need CMake and a C++17 compiler.
# Governance tests need the .NET 8 SDK. Desktop tests need Node.js.

$ErrorActionPreference = "Continue"
$root = Split-Path -Parent $PSScriptRoot
$failed = $false

function Invoke-Step {
    param($Name, [scriptblock]$Action)
    Write-Host ""
    Write-Host "== $Name =="
    & $Action
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAIL $Name"
        $script:failed = $true
    } else {
        Write-Host "PASS $Name"
    }
}

Invoke-Step "LDP protocol" {
    $build = Join-Path $root "ldp\tests\build"
    cmake -S (Join-Path $root "ldp\tests") -B $build
    if ($LASTEXITCODE -ne 0) { return }
    cmake --build $build --config Release
    if ($LASTEXITCODE -ne 0) { return }
    $exe = Join-Path $build "Release\ldp_unit_tests.exe"
    if (-not (Test-Path $exe)) { $exe = Join-Path $build "ldp_unit_tests.exe" }
    & $exe
    if ($LASTEXITCODE -ne 0) { return }
    $ldp1 = Join-Path $build "Release\ldp1_tests.exe"
    if (-not (Test-Path $ldp1)) { $ldp1 = Join-Path $build "ldp1_tests.exe" }
    if (Test-Path $ldp1) { & $ldp1 }
}

Invoke-Step "Governance" {
    dotnet test (Join-Path $root "SynapseMFT-Governance\tests\SynapseMFT.Core.Tests\SynapseMFT.Core.Tests.csproj") --nologo
}

Invoke-Step "Desktop agent" {
    Push-Location (Join-Path $root "ldp-desktop")
    try {
        node --test test/agent.test.js test/governance-client.test.js
    } finally {
        Pop-Location
    }
}

Write-Host ""
if ($failed) {
    Write-Host "Suite finished with failures."
    exit 1
}

Write-Host "Suite finished."
exit 0
