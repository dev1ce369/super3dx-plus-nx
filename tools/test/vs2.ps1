# Players on this PC, a LAN versus: vs2.ps1 [-Exe build\windows\super3dx.exe] [-Data data] [-Players 2] [-Km 2] [-Quit 240] [-Shots '100,130'] [-Name x] [-Host_ 'MT3DX_X=1;...'] [-Client '...'] [-Client2 '...'] [-Hud arcade|touch] [-Option <an options file to start from>]
# The host hosts and starts once the others are in, every car drives itself (MT3DX_AUTODRIVE); logs and shots in build\test-runs\<name>-host, -client, -client2.
# -Expect n: the host waits for n machines (another PC or a Pi makes up the rest: -Players 1 -Expect 2).
# Per-player variables go in -Host_/-Client/-Client2 (MT3DX_AUTODRIVE_KMH=..., MT3DX_AUTOQUIT=... to have one leave on the way).
# The test hooks it sets are the player's: MT3DX_GOTO (a screen), MT3DX_VSAUTO (host/join without a person), MT3DX_VSKM (a short race), MT3DX_SCRIPT, MT3DX_SHOT.
param([string]$Exe = '', [string]$Data = '', [int]$Players = 2, [int]$Expect = 0, [int]$Km = 2, [int]$Quit = 240, [string]$Shots = '100', [string]$ClientShots = '', [string]$Name = 'vs2',
      [string]$Host_ = '', [string]$Client = '', [string]$Client2 = '', [string]$Size = '640x480', [string]$Speed = '', [string]$Hud = 'arcade', [string]$Option = '')
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not $Exe) { $Exe = Join-Path $repo 'build\windows\super3dx.exe' }
if (-not $Data) { $Data = Join-Path $repo 'data' }
$exeName = $Exe; $exeDir = Split-Path $Exe -Parent; $data = $Data # (PowerShell's names ignore case: $exe would have overwritten $Exe)
$runs = Join-Path $repo 'build\test-runs'
Set-Location $exeDir
if (-not $ClientShots) { $ClientShots = $Shots }
$procs = @()
$roles = @('host', 'client', 'client2')[0..($Players - 1)]
foreach ($role in $roles) {
  $dir = "$runs\$Name-$role"
  Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue
  New-Item -ItemType Directory -Force $dir | Out-Null
  Remove-Item -Recurse -Force "$runs\save-$Name$role" -ErrorAction SilentlyContinue
  New-Item -ItemType Directory -Force "$runs\save-$Name$role" | Out-Null
  if ($Option) { Copy-Item $Option "$runs\save-$Name$role\option" }
  $env:MT3DX_LOG = '1'; $env:MT3DX_HQ = '0'; $env:MT3DX_AUTOSTART = '3'; $env:MT3DX_AUTOQUIT = "$Quit"
  $env:MT3DX_SHOT = $(if ($role -eq 'host') { $Shots } else { $ClientShots }); $env:MT3DX_SHOT_DIR = $dir
  $env:MT3DX_SCRIPT = '25:@OK+;37:@OK+;'
  $env:MT3DX_GOTO = '36'; $env:MT3DX_VSKM = "$Km"; $env:MT3DX_AUTODRIVE = '1'
  if ($Speed) { $env:MT3DX_AUTODRIVE_KMH = $Speed }
  $env:MT3DX_VSAUTO = $(if ($role -eq 'host') { "host:$(if ($Expect) { $Expect } else { $Players })" } else { 'join' })
  $extra = $(if ($role -eq 'host') { $Host_ } elseif ($role -eq 'client') { $Client } else { $Client2 })
  foreach ($kv in ($extra -split ';' | ? { $_ })) { $k, $v = $kv -split '=', 2; Set-Item "Env:$k" $v }
  $procs += Start-Process $exeName -ArgumentList "`"$data`"", '--save', "$runs\save-$Name$role", '--size', $Size, '--hud', $Hud -RedirectStandardError "$dir\log.txt" -PassThru -WindowStyle Normal
  foreach ($kv in ($extra -split ';' | ? { $_ })) { $k = ($kv -split '=', 2)[0]; Remove-Item "Env:$k" -ErrorAction SilentlyContinue }
  Start-Sleep 4
}
foreach ($p in $procs) { if (-not $p.WaitForExit(($Quit + 40) * 1000)) { $p.Kill() } }
Remove-Item Env:MT3DX_SCRIPT, Env:MT3DX_SHOT, Env:MT3DX_GOTO, Env:MT3DX_VSAUTO, Env:MT3DX_VSKM, Env:MT3DX_AUTODRIVE, Env:MT3DX_AUTODRIVE_KMH -ErrorAction SilentlyContinue
foreach ($role in $roles) { "== $role"; Select-String -Path "$runs\$Name-$role\log.txt" -Pattern 'versus:|screen \d+ -> ' | % { $_.Line } | Select -Last 14 }
