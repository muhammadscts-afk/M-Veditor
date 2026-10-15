<#
.SYNOPSIS
  Builds M Veditor on Windows and produces: dist\M Veditor\ (app folder), "M Veditor Portable.zip", "M Veditor Setup.exe".

.PARAMETER QtDir      Qt kit directory, e.g. C:\Qt\6.7.3\msvc2019_64
.PARAMETER Flavor     FFmpeg flavor to bundle: "gpl" (H.264/H.265 via x264/x265, full features) or "lgpl" (no x264/x265).
.PARAMETER FfmpegUrl  Override the FFmpeg download URL (pin a specific build for reproducible releases).
.PARAMETER SkipInstaller  Skip the Inno Setup step.
#>
param(
  [Parameter(Mandatory = $true)][string]$QtDir,
  [ValidateSet("gpl", "lgpl")][string]$Flavor = "gpl",
  [string]$FfmpegUrl = "",
  [switch]$SkipInstaller,
  [switch]$WithTests,
  [switch]$AllowSelfTestFailure
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$appName = "M Veditor"
$dist = Join-Path $root "dist"
$appDir = Join-Path $dist $appName
$build = Join-Path $root "build-win"

Write-Host "== 1/6 Configure and build (Release)"
cmake -S . -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$QtDir" "-DMV_BUILD_TESTS=$(if ($WithTests) { "ON" } else { "OFF" })"
cmake --build $build --config Release
$exe = Join-Path $build "$appName.exe"
if (-not (Test-Path $exe)) { throw "Build did not produce $exe" }

Write-Host "== 2/6 Collect application files"
if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory -Force $appDir | Out-Null
Copy-Item $exe $appDir
& "$QtDir\bin\windeployqt.exe" --release --compiler-runtime --no-translations --no-system-d3d-compiler --no-opengl-sw "$appDir\$appName.exe"

Write-Host "== 3/6 Bundle FFmpeg ($Flavor flavor)"
$zip = Join-Path $env:TEMP "ffmpeg-bundle.zip"
$ex = Join-Path $env:TEMP "ffmpeg-bundle"
if (Test-Path $ex) { Remove-Item $ex -Recurse -Force }
$ProgressPreference = "SilentlyContinue"
if ($FfmpegUrl -ne "") { $urls = @($FfmpegUrl) }
else {
  $urls = @(
    "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-$Flavor.zip",
    "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-n7.1-latest-win64-$Flavor-7.1.zip"
  )
  if ($Flavor -eq "gpl") { $urls += "https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip" }
}
$downloaded = $false
foreach ($u in $urls) {
  try {
    Write-Host "Downloading FFmpeg from $u"
    Invoke-WebRequest -Uri $u -OutFile $zip
    $FfmpegUrl = $u
    $downloaded = $true
    break
  } catch {
    Write-Warning "Download failed: $u ($($_.Exception.Message))"
  }
}
if (-not $downloaded) { throw "Could not download FFmpeg from any known address. Pass -FfmpegUrl with a working link to a Windows FFmpeg zip." }
Expand-Archive $zip -DestinationPath $ex
$bin = Get-ChildItem $ex -Recurse -Filter ffmpeg.exe | Select-Object -First 1
if (-not $bin) { throw "ffmpeg.exe not found in the downloaded archive" }
$ffDir = Join-Path $appDir "ffmpeg"
New-Item -ItemType Directory -Force $ffDir | Out-Null
Copy-Item (Join-Path $bin.DirectoryName "ffmpeg.exe") $ffDir
Copy-Item (Join-Path $bin.DirectoryName "ffprobe.exe") $ffDir
$lic = Get-ChildItem $ex -Recurse -Filter "LICENSE*" | Select-Object -First 1
if ($lic) { Copy-Item $lic.FullName (Join-Path $ffDir "FFMPEG-LICENSE.txt") }
"Flavor: $Flavor`nSource archive: $FfmpegUrl`nFFmpeg and codec sources: https://github.com/BtbN/FFmpeg-Builds and https://ffmpeg.org/download.html" |
  Set-Content (Join-Path $ffDir "FFMPEG-SOURCE-INFO.txt")

Write-Host "== 4/6 Documentation"
$docs = Join-Path $appDir "docs"
New-Item -ItemType Directory -Force $docs | Out-Null
Copy-Item "docs\*.md" $docs
Copy-Item "LICENSE" $appDir

Write-Host "== 5/6 Self-test of the packaged app (offline, no dev tools on PATH)"
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
$p = Start-Process -FilePath "$appDir\$appName.exe" -ArgumentList "--selftest" -Wait -PassThru
if (Test-Path "$appDir\selftest.log") { Get-Content "$appDir\selftest.log" }
if ($p.ExitCode -ne 0) {
  if ($AllowSelfTestFailure) { Write-Warning "Self-test FAILED (exit code $($p.ExitCode)) - continuing because -AllowSelfTestFailure was given" }
  else { throw "Self-test FAILED (exit code $($p.ExitCode))" }
}
Remove-Item "$appDir\selftest.log" -ErrorAction SilentlyContinue

Write-Host "== 6/6 Portable zip and installer"
$portableDir = Join-Path $dist "$appName Portable"
Copy-Item $appDir $portableDir -Recurse
"portable" | Set-Content (Join-Path $portableDir "portable.flag")
Compress-Archive -Path $portableDir -DestinationPath (Join-Path $dist "$appName Portable.zip") -Force
Remove-Item $portableDir -Recurse -Force
if (-not $SkipInstaller) {
  $iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
  if (-not $iscc) { Write-Warning "Inno Setup 6 not found - install it (choco install innosetup) to build the installer."; }
  else { & $iscc "/DAppDir=$appDir" "/DOutDir=$dist" "installer\MVeditor.iss" }
}
Write-Host "Done. Output in $dist"
Get-ChildItem $dist | Format-Table Name, Length
