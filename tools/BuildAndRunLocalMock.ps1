param(
    [switch] $DemoReady
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'

if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio Installer vswhere.exe was not found.'
}

$visualStudio = & $vswhere `
    -latest `
    -products '*' `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath

if (-not $visualStudio) {
    throw 'Visual Studio with the C++ desktop tools was not found.'
}

$vcvars = Join-Path $visualStudio 'VC\Auxiliary\Build\vcvars64.bat'
$cmake = Join-Path $visualStudio 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja = Join-Path $visualStudio 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'

foreach ($required in @($vcvars, $cmake, $ninja)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required Visual Studio tool was not found: $required"
    }
}

# Import the x64 compiler environment into this process. Windows may expose
# both PATH and Path; preserve the uppercase PATH produced by vcvars64.bat.
$environmentLines = cmd.exe /d /c "`"$vcvars`" >nul && set"
foreach ($line in $environmentLines) {
    if ($line -cmatch '^([^=]+)=(.*)$' -and $matches[1] -cne 'Path') {
        [System.Environment]::SetEnvironmentVariable(
            $matches[1],
            $matches[2],
            'Process'
        )
    }
}
$pathLine = $environmentLines |
    Where-Object { $_ -clike 'PATH=*' } |
    Select-Object -First 1
if ($pathLine) {
    $env:Path = $pathLine.Substring(5)
}

$buildDirectory = Join-Path $projectRoot 'build-local-gui'

& $cmake `
    -S $projectRoot `
    -B $buildDirectory `
    -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$ninja" `
    -DVTRXP_USE_MOCK_BACKEND=ON `
    -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration failed with exit code $LASTEXITCODE"
}

& $cmake --build $buildDirectory
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed with exit code $LASTEXITCODE"
}

$executable = Join-Path $buildDirectory 'vtrxp_mock_gui.exe'
if (-not (Test-Path -LiteralPath $executable)) {
    throw "GUI executable was not produced: $executable"
}

$packagedExecutable = Join-Path $projectRoot 'vtrxp_mock_gui.exe'
Copy-Item -LiteralPath $executable -Destination $packagedExecutable -Force
$executable = $packagedExecutable

$arguments = if ($DemoReady) { '--demo-ready' } else { '' }
$process = Start-Process `
    -FilePath $executable `
    -ArgumentList $arguments `
    -WorkingDirectory $projectRoot `
    -PassThru
$process.WaitForExit()
exit $process.ExitCode
