# GTee IO Bridge - button mapper GUI.
# Talks to the Pico over its serial (COM) port, reads the current mapping, lets you change it, and saves it
# to the Pico's flash. Nothing else needs to be installed. Start it with gtee-mapper.bat (or right-click > Run with PowerShell).
# Close GTee Clubhouse / any serial terminal first: only one program can have the COM port open.

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

# ------------------------------------------------------------------ action list (type, code)
# type: 0 none, 1 keyboard key (USB HID usage), 2 mouse button, 3 media key
$Actions = New-Object System.Collections.ArrayList
function Add-Action([string]$name, [int]$type, [int]$code) { [void]$Actions.Add([pscustomobject]@{ Name = $name; Type = $type; Code = $code }) }
Add-Action '(nothing)' 0 0
Add-Action 'Mouse: Left click'   2 1
Add-Action 'Mouse: Right click'  2 2
Add-Action 'Mouse: Middle click' 2 4
Add-Action 'Media: Volume Up'    3 233
Add-Action 'Media: Volume Down'  3 234
Add-Action 'Media: Mute'         3 226
Add-Action 'Media: Play/Pause'   3 205
Add-Action 'Key: Enter'      1 40
Add-Action 'Key: Escape'     1 41
Add-Action 'Key: Backspace'  1 42
Add-Action 'Key: Tab'        1 43
Add-Action 'Key: Space'      1 44
Add-Action 'Key: Up arrow'    1 82
Add-Action 'Key: Down arrow'  1 81
Add-Action 'Key: Left arrow'  1 80
Add-Action 'Key: Right arrow' 1 79
Add-Action 'Key: Insert'     1 73
Add-Action 'Key: Home'       1 74
Add-Action 'Key: Page Up'    1 75
Add-Action 'Key: Delete'     1 76
Add-Action 'Key: End'        1 77
Add-Action 'Key: Page Down'  1 78
Add-Action 'Key: Left Ctrl'  1 224
Add-Action 'Key: Left Shift' 1 225
Add-Action 'Key: Left Alt'   1 226
for ($i = 1; $i -le 12; $i++) { Add-Action "Key: F$i" 1 (57 + $i) }
for ($i = 0; $i -lt 26; $i++) { Add-Action ("Key: " + [char](65 + $i)) 1 (4 + $i) }
for ($i = 1; $i -le 9; $i++)  { Add-Action "Key: $i" 1 (29 + $i) }
Add-Action 'Key: 0' 1 39

function Find-ActionIndex([int]$type, [int]$code) {
  for ($i = 0; $i -lt $Actions.Count; $i++) { if ($Actions[$i].Type -eq $type -and $Actions[$i].Code -eq $code) { return $i } }
  if ($type -eq 0) { return 0 }
  # Unknown code set some other way: add it so it isn't lost.
  $label = switch ($type) { 1 { "Key: HID code $code" } 2 { "Mouse: button mask $code" } default { "Media: code $code" } }
  Add-Action $label $type $code
  foreach ($cb in $script:ActionBoxes) { [void]$cb.Items.Add($label) }
  return $Actions.Count - 1
}

$ButtonNames = @('Start/Replay', 'Left', 'Right', 'Fly-by', 'Spin', 'Help', 'Options', 'Service/Test')

# ------------------------------------------------------------------ serial
$script:Port = $null

function Send-Command([string]$cmd, [string]$expect) {
  if (-not $script:Port -or -not $script:Port.IsOpen) { throw 'Not connected' }
  $script:Port.DiscardInBuffer()
  $script:Port.Write("!$cmd`n")
  $deadline = [DateTime]::Now.AddSeconds(3)
  while ([DateTime]::Now -lt $deadline) {
    try { $line = $script:Port.ReadLine().Trim() } catch { if (-not $script:Port.IsOpen) { throw } ; continue }   # read timeout: keep waiting
    if ($line.StartsWith('ERR')) { throw "Pico says: $line" }
    if ($line.StartsWith($expect)) { return $line }
    # anything else is log output from the bridge: ignore
  }
  throw "No reply to '$cmd' (is this the GTee bridge, running the new firmware?)"
}

# ------------------------------------------------------------------ form
$form = New-Object System.Windows.Forms.Form
$form.Text = 'GTee IO Bridge - Button Mapper'
$form.Size = New-Object System.Drawing.Size(560, 640)
$form.StartPosition = 'CenterScreen'
$form.FormBorderStyle = 'FixedDialog'
$form.MaximizeBox = $false
$form.Font = New-Object System.Drawing.Font('Segoe UI', 9)

function New-Label($text, $x, $y, $w = 120) {
  $l = New-Object System.Windows.Forms.Label
  $l.Text = $text; $l.Location = New-Object System.Drawing.Point($x, $y); $l.Size = New-Object System.Drawing.Size($w, 22)
  $l.TextAlign = 'MiddleLeft'; return $l
}

# Connection row
$form.Controls.Add((New-Label 'COM port:' 12 12 70))
$cbPort = New-Object System.Windows.Forms.ComboBox
$cbPort.Location = New-Object System.Drawing.Point(85, 12); $cbPort.Size = New-Object System.Drawing.Size(100, 22); $cbPort.DropDownStyle = 'DropDownList'
$form.Controls.Add($cbPort)
$btnRefresh = New-Object System.Windows.Forms.Button
$btnRefresh.Text = 'Refresh'; $btnRefresh.Location = New-Object System.Drawing.Point(195, 11); $btnRefresh.Size = New-Object System.Drawing.Size(75, 25)
$form.Controls.Add($btnRefresh)
$btnConnect = New-Object System.Windows.Forms.Button
$btnConnect.Text = 'Connect'; $btnConnect.Location = New-Object System.Drawing.Point(280, 11); $btnConnect.Size = New-Object System.Drawing.Size(90, 25)
$form.Controls.Add($btnConnect)

# Buttons group
$grp = New-Object System.Windows.Forms.GroupBox
$grp.Text = 'Cabinet buttons  (the dot lights up while the button is pressed)'
$grp.Location = New-Object System.Drawing.Point(12, 48); $grp.Size = New-Object System.Drawing.Size(520, 290)
$form.Controls.Add($grp)

$script:ActionBoxes = @()
$script:Lights = @()
for ($i = 0; $i -lt 8; $i++) {
  $y = 25 + $i * 32
  $light = New-Object System.Windows.Forms.Panel
  $light.Location = New-Object System.Drawing.Point(14, ($y + 5)); $light.Size = New-Object System.Drawing.Size(12, 12)
  $light.BackColor = [System.Drawing.Color]::Gainsboro; $light.BorderStyle = 'FixedSingle'
  $grp.Controls.Add($light); $script:Lights += $light
  $grp.Controls.Add((New-Label $ButtonNames[$i] 34 $y 110))
  $cb = New-Object System.Windows.Forms.ComboBox
  $cb.Location = New-Object System.Drawing.Point(150, $y); $cb.Size = New-Object System.Drawing.Size(250, 22); $cb.DropDownStyle = 'DropDownList'
  $cb.MaxDropDownItems = 20
  foreach ($a in $Actions) { [void]$cb.Items.Add($a.Name) }
  $grp.Controls.Add($cb); $script:ActionBoxes += $cb
}

# Volume combo group
$grpVol = New-Object System.Windows.Forms.GroupBox
$grpVol.Text = 'Windows volume combo'
$grpVol.Location = New-Object System.Drawing.Point(12, 345); $grpVol.Size = New-Object System.Drawing.Size(520, 95)
$form.Controls.Add($grpVol)
$chkVol = New-Object System.Windows.Forms.CheckBox
$chkVol.Text = 'Enabled'; $chkVol.Location = New-Object System.Drawing.Point(14, 22); $chkVol.Size = New-Object System.Drawing.Size(80, 22)
$grpVol.Controls.Add($chkVol)
function New-ButtonPicker($x, $y) {
  $c = New-Object System.Windows.Forms.ComboBox
  $c.Location = New-Object System.Drawing.Point($x, $y); $c.Size = New-Object System.Drawing.Size(110, 22); $c.DropDownStyle = 'DropDownList'
  foreach ($n in $ButtonNames) { [void]$c.Items.Add($n) }
  return $c
}
$grpVol.Controls.Add((New-Label 'Hold' 100 22 40))
$cbVolMod = New-ButtonPicker 140 22; $grpVol.Controls.Add($cbVolMod)
$grpVol.Controls.Add((New-Label 'and press' 260 22 70))
$grpVol.Controls.Add((New-Label 'Volume down:' 100 55 85))
$cbVolDown = New-ButtonPicker 190 55; $grpVol.Controls.Add($cbVolDown)
$grpVol.Controls.Add((New-Label 'Volume up:' 315 55 70))
$cbVolUp = New-ButtonPicker 390 55; $grpVol.Controls.Add($cbVolUp)

# Mouse group
$grpMouse = New-Object System.Windows.Forms.GroupBox
$grpMouse.Text = 'Trackball'
$grpMouse.Location = New-Object System.Drawing.Point(12, 447); $grpMouse.Size = New-Object System.Drawing.Size(520, 55)
$form.Controls.Add($grpMouse)
$grpMouse.Controls.Add((New-Label 'Speed (x):' 14 20 65))
$numScale = New-Object System.Windows.Forms.NumericUpDown
$numScale.Location = New-Object System.Drawing.Point(80, 21); $numScale.Size = New-Object System.Drawing.Size(50, 22); $numScale.Minimum = 1; $numScale.Maximum = 10; $numScale.Value = 1
$grpMouse.Controls.Add($numScale)
$chkInvX = New-Object System.Windows.Forms.CheckBox
$chkInvX.Text = 'Invert X'; $chkInvX.Location = New-Object System.Drawing.Point(160, 20); $chkInvX.Size = New-Object System.Drawing.Size(80, 22)
$grpMouse.Controls.Add($chkInvX)
$chkInvY = New-Object System.Windows.Forms.CheckBox
$chkInvY.Text = 'Invert Y'; $chkInvY.Location = New-Object System.Drawing.Point(250, 20); $chkInvY.Size = New-Object System.Drawing.Size(80, 22)
$grpMouse.Controls.Add($chkInvY)

# Action buttons
$btnRead = New-Object System.Windows.Forms.Button
$btnRead.Text = 'Read from Pico'; $btnRead.Location = New-Object System.Drawing.Point(12, 512); $btnRead.Size = New-Object System.Drawing.Size(120, 30)
$form.Controls.Add($btnRead)
$btnDefaults = New-Object System.Windows.Forms.Button
$btnDefaults.Text = 'Factory defaults'; $btnDefaults.Location = New-Object System.Drawing.Point(140, 512); $btnDefaults.Size = New-Object System.Drawing.Size(120, 30)
$form.Controls.Add($btnDefaults)
$btnSave = New-Object System.Windows.Forms.Button
$btnSave.Text = 'Save to Pico'; $btnSave.Location = New-Object System.Drawing.Point(392, 512); $btnSave.Size = New-Object System.Drawing.Size(140, 30)
$btnSave.Font = New-Object System.Drawing.Font('Segoe UI', 9, [System.Drawing.FontStyle]::Bold)
$form.Controls.Add($btnSave)

$status = New-Object System.Windows.Forms.Label
$status.Location = New-Object System.Drawing.Point(12, 552); $status.Size = New-Object System.Drawing.Size(520, 40)
$status.Text = 'Pick the Pico''s COM port and click Connect.'
$form.Controls.Add($status)

$editControls = @($script:ActionBoxes) + @($chkVol, $cbVolMod, $cbVolDown, $cbVolUp, $numScale, $chkInvX, $chkInvY, $btnRead, $btnDefaults, $btnSave)
function Set-Editable([bool]$on) { foreach ($c in $editControls) { $c.Enabled = $on } }
Set-Editable $false

# ------------------------------------------------------------------ logic
function Show-Config([string]$line) {
  # CFG btn0=1,40 btn1=2,1 ... vol=1,3,1,2 mouse=1,0,1
  foreach ($tok in $line.Split(' ')) {
    if ($tok -match '^btn(\d)=(\d+),(\d+)$') {
      $idx = Find-ActionIndex ([int]$Matches[2]) ([int]$Matches[3])
      $script:ActionBoxes[[int]$Matches[1]].SelectedIndex = $idx
    } elseif ($tok -match '^vol=(\d+),(\d+),(\d+),(\d+)$') {
      $chkVol.Checked = $Matches[1] -ne '0'
      $cbVolMod.SelectedIndex = [int]$Matches[2]; $cbVolDown.SelectedIndex = [int]$Matches[3]; $cbVolUp.SelectedIndex = [int]$Matches[4]
    } elseif ($tok -match '^mouse=(\d+),(\d+),(\d+)$') {
      $numScale.Value = [int]$Matches[1]; $chkInvX.Checked = $Matches[2] -ne '0'; $chkInvY.Checked = $Matches[3] -ne '0'
    }
  }
}

function Read-Config { Show-Config (Send-Command 'get' 'CFG') }

function Refresh-Ports {
  $cbPort.Items.Clear()
  foreach ($p in [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object -Unique) { [void]$cbPort.Items.Add($p) }
  if ($cbPort.Items.Count) { $cbPort.SelectedIndex = $cbPort.Items.Count - 1 }
}

function Disconnect {
  $timer.Stop()
  if ($script:Port) { try { $script:Port.Close() } catch {} ; $script:Port = $null }
  $btnConnect.Text = 'Connect'; Set-Editable $false
  foreach ($l in $script:Lights) { $l.BackColor = [System.Drawing.Color]::Gainsboro }
}

$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 150
$timer.Add_Tick({
  try {
    $m = [int64](Send-Command 'state' 'STATE').Substring(6)
    for ($i = 0; $i -lt 8; $i++) {
      $script:Lights[$i].BackColor = if ($m -band (1 -shl $i)) { [System.Drawing.Color]::LimeGreen } else { [System.Drawing.Color]::Gainsboro }
    }
  } catch { Disconnect; $status.Text = "Lost connection: $($_.Exception.Message)" }
})

$btnRefresh.Add_Click({ Refresh-Ports })

$btnConnect.Add_Click({
  if ($script:Port) { Disconnect; $status.Text = 'Disconnected.'; return }
  if (-not $cbPort.SelectedItem) { $status.Text = 'No COM port selected.'; return }
  try {
    # 115200 baud. Never use 1200: that baud rate tells the Pico to reboot into its bootloader.
    $p = New-Object System.IO.Ports.SerialPort ([string]$cbPort.SelectedItem), 115200
    $p.NewLine = "`n"; $p.ReadTimeout = 500; $p.WriteTimeout = 1000; $p.DtrEnable = $true; $p.RtsEnable = $true
    $p.Open(); $script:Port = $p
    Start-Sleep -Milliseconds 200
    Read-Config
    $btnConnect.Text = 'Disconnect'; Set-Editable $true; $timer.Start()
    $status.Text = "Connected to $($cbPort.SelectedItem). Change what you like, then click Save to Pico."
  } catch {
    Disconnect
    $status.Text = "Could not connect: $($_.Exception.Message)"
  }
})

$btnRead.Add_Click({
  try { Send-Command 'reload' 'OK' | Out-Null; Read-Config; $status.Text = 'Loaded the mapping saved on the Pico.' }
  catch { $status.Text = $_.Exception.Message }
})

$btnDefaults.Add_Click({
  try { Send-Command 'defaults' 'OK' | Out-Null; Read-Config; $status.Text = 'Factory defaults loaded (and active). Click Save to Pico to keep them.' }
  catch { $status.Text = $_.Exception.Message }
})

$btnSave.Add_Click({
  if ($chkVol.Checked) {
    $sel = @($cbVolMod.SelectedIndex, $cbVolDown.SelectedIndex, $cbVolUp.SelectedIndex)
    if (($sel | Sort-Object -Unique).Count -ne 3) { $status.Text = 'Volume combo: hold / down / up must be three different buttons.'; return }
  }
  $timer.Stop()
  try {
    for ($i = 0; $i -lt 8; $i++) {
      $a = $Actions[$script:ActionBoxes[$i].SelectedIndex]
      Send-Command "set btn$i=$($a.Type),$($a.Code)" 'OK' | Out-Null
    }
    $v = if ($chkVol.Checked) { 1 } else { 0 }
    if ($cbVolMod.SelectedIndex -ge 0 -and $cbVolDown.SelectedIndex -ge 0 -and $cbVolUp.SelectedIndex -ge 0 -and
        (@($cbVolMod.SelectedIndex, $cbVolDown.SelectedIndex, $cbVolUp.SelectedIndex) | Sort-Object -Unique).Count -eq 3) {
      Send-Command "set vol=$v,$($cbVolMod.SelectedIndex),$($cbVolDown.SelectedIndex),$($cbVolUp.SelectedIndex)" 'OK' | Out-Null
    }
    $ix = if ($chkInvX.Checked) { 1 } else { 0 }; $iy = if ($chkInvY.Checked) { 1 } else { 0 }
    Send-Command "set mouse=$([int]$numScale.Value),$ix,$iy" 'OK' | Out-Null
    Send-Command 'save' 'OK' | Out-Null
    Read-Config
    $status.Text = "Saved to the Pico's flash at $(Get-Date -Format T). It stays after power-off."
  } catch { $status.Text = "Save failed: $($_.Exception.Message)" }
  if ($script:Port) { $timer.Start() }
})

$form.Add_FormClosing({ Disconnect })
Refresh-Ports
[void]$form.ShowDialog()
