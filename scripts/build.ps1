# Configure, compile and run regressions before publishing a local distribution directory.
param(
    [string]$WxRoot = "",
    [ValidateSet("Debug", "Release")][string]$Configuration = "Release",
    [switch]$Package,
    [switch]$GuiTest,
    [switch]$ConfigureOnly
)
$ErrorActionPreference = 'Stop'
$edaRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$edaBuild = Join-Path $edaRoot 'build'
$edaCmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $edaCmake) {
    $edaVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $edaVsWhere) {
        $edaVs = & $edaVsWhere -latest -products '*' -property installationPath
        $edaCmake = Join-Path $edaVs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    }
}
if (-not $edaCmake -or -not (Test-Path -LiteralPath $edaCmake)) {
    throw 'Install CMake 3.20+ or Visual Studio 2022 with Desktop development with C++.'
}
if (-not $WxRoot) {
    $edaCandidate = Join-Path (Split-Path $edaRoot -Parent) 'wxWidgets'
    if (Test-Path -LiteralPath (Join-Path $edaCandidate 'include\wx\wx.h')) { $WxRoot = $edaCandidate }
}
$edaConfigure = @('-S', $edaRoot, '-B', $edaBuild, '-G', 'Visual Studio 17 2022', '-A', 'x64')
if ($WxRoot) {
    $edaConfigure += "-DwxWidgets_ROOT_DIR=$WxRoot"
    $edaConfigure += "-DwxWidgets_LIB_DIR=$(Join-Path $WxRoot 'lib\vc_x64_lib')"
}
& $edaCmake @edaConfigure
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
if ($ConfigureOnly) {
    Write-Host "Open in Visual Studio: $(Join-Path $edaBuild 'PracticeEDA.sln')"
    return
}
& $edaCmake --build $edaBuild --config $Configuration --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
$edaCTest = Join-Path (Split-Path $edaCmake -Parent) 'ctest.exe'
& $edaCTest --test-dir $edaBuild -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Core regression tests failed.' }
if ($GuiTest) {
    # Remove only stale result markers so a previous passing run cannot hide a new failure.
    $edaQa = Join-Path $edaBuild ('qa-' + $Configuration.ToLowerInvariant())
    foreach ($edaMarker in @('PASS.txt', 'LOGIC-PASS.txt', 'FAILED.txt')) {
        $edaMarkerPath = Join-Path $edaQa $edaMarker
        if (Test-Path -LiteralPath $edaMarkerPath) { Remove-Item -LiteralPath $edaMarkerPath }
    }
    $edaExecutable = Join-Path $edaBuild "$Configuration\PracticeEDA.exe"
    $edaProcess = Start-Process -FilePath $edaExecutable -ArgumentList @('--smoke-test', ('"' + $edaQa + '"')) -WindowStyle Hidden -PassThru
    if (-not $edaProcess.WaitForExit(60000)) { throw 'GUI smoke test did not finish within 60 seconds.' }
    if ($edaProcess.ExitCode -ne 0 -or
        -not (Test-Path -LiteralPath (Join-Path $edaQa 'LOGIC-PASS.txt')) -or
        (Test-Path -LiteralPath (Join-Path $edaQa 'FAILED.txt'))) {
        throw "GUI smoke test failed. Inspect $edaQa"
    }
    Get-Content -LiteralPath (Join-Path $edaQa 'LOGIC-PASS.txt')
}
if ($Package) {
    & $edaCmake --install $edaBuild --config $Configuration --prefix (Join-Path $edaRoot 'dist\PracticeEDA')
    if ($LASTEXITCODE -ne 0) { throw 'Packaging failed.' }
}
Write-Host "Ready: $(Join-Path $edaBuild "$Configuration\PracticeEDA.exe")"
