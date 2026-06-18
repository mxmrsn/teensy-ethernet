# flash_teensy.ps1 -- one-elevation Teensy flash + WSL re-attach (Windows side).
#
# Why this exists: true WSL flashing (teensy_loader_cli) fails on this machine --
# the HalfKay bootloader device won't attach into WSL (CsDeviceControl USB
# security filter). So we flash from Windows, which handles the bootloader
# natively, and hand the device back to WSL afterward. The whole cycle runs in
# ONE elevation, so iterating costs a single UAC click (no button press).
#
# Run it elevated (the launcher below does that for you):
#   powershell -ExecutionPolicy Bypass -File flash_teensy.ps1
#
param(
  [string]$Hex   = "C:\teensy_build\microros_serial_test\firmware.hex",
  [string]$BusId = "7-1",
  [string]$Port  = "COM4"
)

$ErrorActionPreference = "Continue"
$usbipd = "C:\Program Files\usbipd-win\usbipd.exe"
$tools  = "$env:USERPROFILE\.platformio\packages\tool-teensy"
$log    = "C:\teensy_build\flash_win.log"
function Log($m) { $line = "{0}  {1}" -f (Get-Date -Format "HH:mm:ss"), $m; $line | Tee-Object -FilePath $log -Append }

"" | Set-Content $log
Log "=== flash start: $Hex ==="

# 0) make sure the usbipd service is up (don't kill usbipd processes broadly --
#    that would take down the service itself)
Start-Service usbipd -ErrorAction SilentlyContinue

# 1) release from WSL + unbind so Windows re-creates the COM port
& $usbipd detach --busid $BusId 2>&1 | Out-Null
& $usbipd unbind --busid $BusId 2>&1 | Out-Null
Start-Sleep -Seconds 3
Log "unbound; COM port should be back"

# 2) flash (teensy_post_compile -reboot auto-reboots the running sketch, no button)
Log "flashing via teensy_post_compile"
$path = Split-Path $Hex
& "$tools\teensy_post_compile.exe" -file=firmware -path="$path" -tools="$tools" -board=TEENSY41 -reboot -port=$Port 2>&1 | ForEach-Object { Log "  $_" }
Start-Sleep -Seconds 6   # let it program + reboot + re-enumerate

# 3) hand the device back to WSL (retry -- the device is still re-enumerating
#    right after the reboot, so a single attach often misses)
& $usbipd bind --force --busid $BusId 2>&1 | Out-Null
for ($i = 0; $i -lt 6; $i++) {
  & $usbipd attach --wsl --busid $BusId 2>&1 | Out-Null
  Start-Sleep -Seconds 2
  if (& $usbipd list | Select-String "$BusId.*Attached") { break }
}
$state = (& $usbipd list | Select-String $BusId) -join "`n"
Log "re-attached: $state"
Log "=== flash done ==="
