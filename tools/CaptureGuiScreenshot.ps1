param(
    [string] $OutputPath,
    [switch] $ErrorState,
    [switch] $DemoReady
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$executable = Join-Path $projectRoot 'vtrxp_mock_gui.exe'
if (-not (Test-Path -LiteralPath $executable)) {
    throw 'The packaged GUI executable was not found.'
}

if (-not $OutputPath) {
    $screenshotDirectory = Join-Path $projectRoot 'screenshots'
    New-Item -ItemType Directory -Force -Path $screenshotDirectory | Out-Null
    $fileName = if ($ErrorState) {
        'vtrxp_mock_gui_error.png'
    }
    elseif ($DemoReady) {
        'vtrxp_mock_gui_demo_ready.png'
    }
    else {
        'vtrxp_mock_gui_demo.png'
    }
    $OutputPath = Join-Path $screenshotDirectory $fileName
}

$outputCandidate = if ([System.IO.Path]::IsPathRooted($OutputPath)) {
    $OutputPath
}
else {
    Join-Path (Get-Location) $OutputPath
}
$resolvedOutputPath = [System.IO.Path]::GetFullPath($outputCandidate)
$outputDirectory = Split-Path -Parent $resolvedOutputPath
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$OutputPath = $resolvedOutputPath

$beforeCapture = @(
    Get-ChildItem -LiteralPath $projectRoot -File `
        -Filter 'vtrxp_mock_gui_capture_*.bmp' -ErrorAction SilentlyContinue |
        Select-Object -ExpandProperty FullName
)

$process = Start-Process `
    -FilePath $executable `
    -ArgumentList $(
        if ($ErrorState) { '--capture-error-bmp' }
        elseif ($DemoReady) { '--capture-demo-ready-bmp' }
        else { '--capture-bmp' }
    ) `
    -WorkingDirectory $projectRoot `
    -WindowStyle Normal `
    -PassThru `
    -Wait

if ($process.ExitCode -ne 0) {
    throw "GUI self-capture failed with exit code $($process.ExitCode)."
}

$captureBitmap = Get-ChildItem -LiteralPath $projectRoot -File `
    -Filter 'vtrxp_mock_gui_capture_*.bmp' -ErrorAction SilentlyContinue |
    Where-Object { $beforeCapture -notcontains $_.FullName } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
if (-not $captureBitmap) {
    throw 'The GUI did not produce its self-captured bitmap.'
}

Add-Type -AssemblyName System.Drawing
$bytes = [System.IO.File]::ReadAllBytes($captureBitmap.FullName)
$stream = New-Object System.IO.MemoryStream(,$bytes)
$sourceBitmap = [System.Drawing.Bitmap]::FromStream($stream)
$bitmap = New-Object System.Drawing.Bitmap $sourceBitmap
$sourceBitmap.Dispose()
$stream.Dispose()
$temporaryPng = "$OutputPath.$PID.new.png"
$bitmap.Save($temporaryPng, [System.Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()
[GC]::Collect()
[GC]::WaitForPendingFinalizers()
Copy-Item -LiteralPath $temporaryPng -Destination $OutputPath -Force
Remove-Item -LiteralPath $temporaryPng -Force
for ($attempt = 0; $attempt -lt 10; $attempt++) {
    try {
        Remove-Item -LiteralPath $captureBitmap.FullName -Force -ErrorAction Stop
        break
    }
    catch {
        Start-Sleep -Milliseconds 200
    }
}

Write-Output $OutputPath
