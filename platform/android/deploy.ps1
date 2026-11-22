<#
Install the APK on a phone on USB (USB debugging on) and put the converted game data on it.

  powershell -File platform\android\deploy.ps1 [-Adb path\to\adb.exe] [-Apk build\android\super3dx-android.apk]
                                               [-Data data] [-SkipApk] [-SkipData]

Why a tarball: "adb push <folder>" of the big folders crashed adb (std::bad_alloc) on the machine this was
made on, and a half-pushed data set looks like a game that runs without sound or car models. So the folders
go as one tar, are extracted on the phone, and every folder's file count is checked against the PC's.
Stops with a message at the first failure, and ends with DONE or FAILED.
#>
param(
  [string]$Adb = "adb",
  [string]$Apk = "build\android\super3dx-android.apk",
  [string]$Data = "data",
  [switch]$SkipApk,
  [switch]$SkipData
)
$ErrorActionPreference = "Stop"
$App = "/sdcard/Android/data/com.wmmt.port/files/wmmt"
$Dirs = "traffic", "carconfig", "cars", "chara", "sound", "wheels", "vinyl", "cutin", "rtmovie"

function Run($what, [scriptblock]$cmd) {
  & $cmd
  if ($LASTEXITCODE -ne 0) { throw "$what failed (exit $LASTEXITCODE)" }
}

try {
  $dev = (& $Adb devices) | Select-String "\tdevice$"
  if (-not $dev) { throw "no authorized device: connect the phone, allow USB debugging, and check 'adb devices'" }

  if (-not $SkipApk) {
    if (-not (Test-Path $Apk)) { throw "no $Apk: run platform/android/build.sh first" }
    Run "install" { & $Adb install -r $Apk }
  }

  if (-not $SkipData) {
    foreach ($d in $Dirs) { if (-not (Test-Path (Join-Path $Data $d))) { throw "missing $Data\$d: run tools/convert_all.sh first" } }
    Run "mkdir" { & $Adb shell "mkdir -p $App" }
    # the top-level files, one by one (they are single files, which adb handles)
    $files = Get-ChildItem $Data -File | Where-Object { $_.Extension -in ".wpk", ".wcl", ".bin", ".names", ".img", ".mp4" }
    foreach ($f in $files) { Run "push $($f.Name)" { & $Adb push $f.FullName "$App/" | Out-Null } }
    # the folders as a tarball (Windows 10 has tar.exe)
    $tar = Join-Path $env:TEMP "super3dx_dirs.tar"
    Push-Location $Data
    try { Run "tar" { tar -cf $tar @Dirs } } finally { Pop-Location }
    Run "push tar" { & $Adb push $tar "/data/local/tmp/super3dx_dirs.tar" | Out-Null }
    Run "extract" { & $Adb shell "cd $App && tar -xf /data/local/tmp/super3dx_dirs.tar && rm /data/local/tmp/super3dx_dirs.tar" }
    Remove-Item $tar -ErrorAction SilentlyContinue
    # every folder's file count must match the PC's
    foreach ($d in $Dirs) {
      $local = (Get-ChildItem (Join-Path $Data $d) -Recurse -File | Measure-Object).Count
      $dev = [int](& $Adb shell "find $App/$d -type f | wc -l")
      if ($local -ne $dev) { throw "$d : $local files here, $dev on the phone" }
      Write-Host ("  {0,-10} {1} files OK" -f $d, $local)
    }
  }
  Write-Host "DONE"
} catch {
  Write-Host "FAILED: $_"
  exit 1
}
