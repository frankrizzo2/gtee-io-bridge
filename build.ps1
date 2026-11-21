# Builds the bridge firmware and optionally uploads it.
#   .\build.ps1                -> compile, writes gtee-bridge.uf2 next to this script
#   .\build.ps1 -Port COM4     -> compile and upload over USB (works when the Pico is already running the bridge)
#   .\build.ps1 -Cli C:\path\arduino-cli.exe   -> use a specific arduino-cli (default: the one on PATH)
# Needs: arduino-cli, the rp2040:rp2040 core (Earle Philhower), and the library "Pico PIO USB" 0.7.3 or newer.
param([string]$Port = '', [switch]$Clean, [string]$Cli = 'arduino-cli')
$fqbn  = 'rp2040:rp2040:rpipico:usbstack=tinyusb,freq=120'
$flags = '-DCFG_TUH_API_EDPT_XFER=1'   # REQUIRED: without it TinyUSB drops tuh_edpt_xfer() completions after the first one
$src   = Join-Path $PSScriptRoot 'gtee-bridge'
$out   = Join-Path $PSScriptRoot 'build'
$a = @('compile', '--fqbn', $fqbn, '--output-dir', $out,
       '--build-property', "compiler.c.extra_flags=$flags",
       '--build-property', "compiler.cpp.extra_flags=$flags")
if ($Clean) { $a += '--clean' }
& $Cli @a $src
if ($LASTEXITCODE -ne 0) { throw "compile failed" }
Copy-Item (Join-Path $out 'gtee-bridge.ino.uf2') (Join-Path $PSScriptRoot 'gtee-bridge.uf2') -Force
"UF2: $(Join-Path $PSScriptRoot 'gtee-bridge.uf2')"
if ($Port) { & $Cli upload --fqbn $fqbn -p $Port --input-dir $out }
