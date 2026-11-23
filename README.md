# gtee-bridge

Use an original **Golden Tee LIVE (Incredible Technologies "Nighthawk") USB I/O board** on a Windows PC, for example with GTee Clubhouse.

A Raspberry Pi Pico sits between the board and the PC. It acts as a **USB host** to the I/O board and shows up on the PC as an ordinary **USB keyboard + mouse**. No drivers, nothing to install on the PC.

- Trackball → mouse movement
- Buttons → keys (default layout matches GTee Clubhouse's Global Game Controls)
- Hold **Help + Options for 4 seconds** → Escape (exit back to Clubhouse)

> **Scope:** this project only reads the controls. It does not touch the security dongle, the game software or any copy protection, and it doesn't include any game files.

**Status:** working. Tested with GTee Clubhouse 0.3 Beta and Golden Tee 2026. Soak-tested for 4 hours (1.8 million reports) with no errors and no restarts, including a cabinet power cycle mid-run.

---

## What you need

- 1 × Raspberry Pi Pico (RP2040; the original Pico, not the Pico 2)
- The I/O board's USB cable (cut, see wiring), or a spare USB cable to cut
- A micro-USB cable from the Pico to the PC
- Soldering iron or 0.1" headers + jumper wires

## Wiring

Cut the USB cable that runs from the I/O board to the cabinet PC and connect the **board's end** to the Pico:

| Board USB wire | Signal | Pico pin |
|---|---|---|
| Green | D+ | **GP10** (physical pin 14) |
| White | D− | **GP11** (physical pin 15) |
| Black | GND | **GND** (physical pin 13) |
| Red | +5 V | **VBUS** (physical pin 40) |

```
                    ┌──────[ USB ]──────┐   ← micro-USB to the PC
              GP0  1│●                 ●│40  VBUS  ◄── RED   (+5 V)
              GP1  2│●                 ●│39  VSYS
BTN GND ───►  GND  3│●                 ●│38  GND        (optional Help/Options
HELP    ───►  GP2  4│●                 ●│37  3V3_EN      buttons, see below)
OPTIONS ───►  GP3  5│●                 ●│36  3V3
              GP4  6│●                 ●│35  ADC_VREF
              GP5  7│●                 ●│34  GP28
              GND  8│●                 ●│33  GND
              GP6  9│●                 ●│32  GP27
              GP7 10│●                 ●│31  GP26
              GP8 11│●                 ●│30  RUN
              GP9 12│●                 ●│29  GP22
 BLACK  ──►   GND 13│●                 ●│28  GND
 GREEN  ──►  GP10 14│●   Raspberry     ●│27  GP21
 WHITE  ──►  GP11 15│●     Pi Pico     ●│26  GP20
             GP12 16│●                 ●│25  GP19
             GP13 17│●                 ●│24  GP18
              GND 18│●                 ●│23  GND
             GP14 19│●                 ●│22  GP17
             GP15 20│●                 ●│21  GP16
                    └───────────────────┘
                     (viewed from the top, USB connector up)
```

Notes:
- **Check the wire colours with a meter** before trusting them; not every cable follows the standard.
- The red wire powers the board's USB side from the PC through the Pico's VBUS pin. If you keep the cabinet-PC end of the cable around, **cut or insulate its red wire** so two 5 V supplies are never joined.
- The I/O board keeps its **cabinet power supply** for the controls. Never connect the cabinet supply to the Pico.
- If the cabinet PC was the last thing to use the board, **power-cycle the cabinet once** before the Pico can see it.

### Optional: Help and Options buttons wired to the Pico

Converting a Silver Strike, PowerPutt or Target Toss pedestal? Those games don't use Help and Options, so the harness often doesn't bring them to the I/O board. Instead of adding wires to the board, you can wire the two buttons straight to the Pico:

| Button | Pico pin |
|---|---|
| Help | **GP2** (physical pin 4) |
| Options | **GP3** (physical pin 5) |
| Both buttons' common/ground side | **GND** (physical pin 3) |

- Each button connects the pin to ground when pressed. The Pico's internal pull-ups are used, so no resistors are needed.
- **3.3 V logic only.** The button must be a plain switch to ground. Don't connect a lamp supply or any cabinet voltage to these pins. Check with a meter first if the button has a lamp.
- These act exactly like Help and Options on the I/O board: same keys (W and Q), and they work in the Help + Options exit combo. If both are wired, either source counts as a press.
- The serial `s` command shows each pin as `up` or `PRESSED`, which is handy for checking the wiring.
- To change or disable the pins, edit `PICO_HELP_PIN` / `PICO_OPTIONS_PIN` in `config.h` (`-1` = off).

## Installing the firmware

1. Hold **BOOTSEL** on the Pico and plug it into the PC. A drive called **RPI-RP2** appears.
2. Copy **`gtee-bridge.uf2`** onto it. The Pico restarts as the bridge.

The LED **blinks** while it waits for the board and goes **solid** once it is receiving reports.

## Using it

When the PC turns on, the Pico starts with it and the controls work within about half a second of the board being powered. No software needs to run on the PC.

Switching the cabinet off and on while the PC is running is fine. The controls drop out while the cabinet is off and come back by themselves a few seconds after it powers up. The keyboard and mouse stay connected to the PC throughout, so the game just sees no input for a moment.

### Default keys

| Cabinet button | Sends | GTee Clubhouse action |
|---|---|---|
| Start/Replay | Enter | Start |
| Left / Right | Left / right **mouse click** | (rebind Left/Right in Clubhouse if you use them there) |
| Fly-by | ↑ | FlyBy |
| Spin | ↓ | Spin |
| Help | W | Help |
| Options | Q | Options |
| Service/Test | F2 | Test (bind once in Clubhouse) |
| Help + Options held 4 s | Escape (one tap) | Exit to Clubhouse |
| Hold Fly-by + press Left / Right | Windows **Volume Down / Up** (repeats while held) | — |

The volume combo uses the standard media-key volume buttons, so it works in Windows itself with no software. Once you use the combo, Fly-by's own key is released until you let go of it.

### Changing the mapping (GUI)

Double-click **`gtee-mapper.bat`** on the PC the Pico is plugged into (close any serial terminal first). Pick the Pico's COM port, click **Connect**, choose what each button sends (any key, left/right/middle mouse click, or a media key), set the volume combo and trackball options, then **Save to Pico**. The dot next to each button lights while you press it, so you can check which is which. The mapping is stored in the Pico's flash and survives power-off; the controls drop out for a moment while it saves. **Factory defaults** restores the table above.

The same thing can be done from a serial terminal with `!get`, `!set …`, `!save` (type `h` for the list).

### GTee Clubhouse setup

Settings → Global Game Controls:
- **Test:** click its binding and press the cabinet's Service/Test button (F2).
- **TrackBall device (relative X/Y) → Find Device:** roll the trackball.
- Keep **Remote pointer compatibility** off.
- Use **Invert X/Y** and the **sensitivity sliders** to tune the swing.
- Leave **Exit hold (seconds)** at 0; the 4-second hold is done by the bridge.

### Serial console (optional)

The Pico also shows up as a COM port (115200 baud):

| Command | Does |
|---|---|
| `s` | Status (reports received, errors) |
| `r` | Print raw reports (useful for mapping a different board) |
| `l` | Log history and connected devices |
| `o` | Turn keyboard/mouse output on/off |

## Customising

Everything board-specific is in [`gtee-bridge/config.h`](gtee-bridge/config.h):
- `BUTTON_INPUTS` / `DEFAULT_ACTIONS`: which report bit is which button, and its factory-default action (the GUI changes the live mapping without a rebuild)
- `VOLUME_COMBO_*`: default volume combo buttons and repeat timing
- `MOUSE_SCALE`, `MOUSE_INVERT_X`, `MOUSE_INVERT_Y` (defaults; also adjustable in the GUI)
- `EXIT_COMBO_MS`, `EXIT_COMBO_KEY`
- `CTRL_VID` / `CTRL_PID` / `CTRL_EP_IN`: which device and endpoint to read

### Building from source

Requirements, all of which matter:
- **arduino-cli** with the **Earle Philhower RP2040 core** (tested with 6.4.0), USB stack **Adafruit TinyUSB**, CPU **120 MHz**
- Library **Pico PIO USB 0.7.3 or newer.** 0.7.2 has a bug that hangs the USB host about every 2 minutes.
- Build flag **`-DCFG_TUH_API_EDPT_XFER=1`.** Without it, only the first report ever arrives. The sketch refuses to compile without it.

```powershell
.\build.ps1                 # writes gtee-bridge.uf2
.\build.ps1 -Port COM4      # build and upload to a Pico already running the bridge
```

## How the board works

| Item | Value |
|---|---|
| Hub | `0344:33AA`, full-speed USB 1.1, 3-port compound hub. Its firmware only returns a valid hub descriptor for `wValue = 0`, which is why Windows shows **Code 43** on a modern PC. TinyUSB's host asks with `wValue = 0`, so it works. |
| Port 1 | `0344:03AB` controls device, interface class `58/42`. EP `0x81` interrupt IN, 12-byte reports at ~125 Hz. EP `0x02` bulk OUT (lamps; not used). |
| Port 3 | `0451:3410` TI TUSB3410 serial chip. Enumerated, then ignored. |

No start-up commands are needed; the bridge just polls EP `0x81`.

**Input report (12 bytes):**

| Byte | Meaning |
|---|---|
| 0 | Sequence counter |
| 1–3 | Slow-changing channel (unused) |
| 4 | Trackball Y, 8-bit wrapping counter |
| 5 | Trackball X, 8-bit wrapping counter |
| 6 | `01` |
| 7 | Buttons, active low: bit0 Start, bit1 Left, bit2 Right, bit3 Fly-by, bit4 Spin, bit5 Help, bit6 Options |
| 8 | bit7 Service/Test, active low |
| 9–11 | `FB FF FF` |

## Other boards

**Green IT I/O board** (the PowerPutt / Silver Strike / Target Toss style board): same hub (`0344:33AA`) and controls device (`0344:03AB`), and it works with the bridge unchanged. Trackball, Left, Right, Fly-by, Spin, Start and Service/Test land on the same bits as the Golden Tee board. Help and Options are on extra harness wires that are often left unconnected on these boards. Either connect them to the board or wire the buttons to the Pico (see *Optional: Help and Options buttons wired to the Pico*). Report differences that don't matter: byte 6 reads `04` (instead of `01`), bytes 9–11 differ, and Start also clears byte 8 bit 6.

Other IT USB I/O boards will probably work with changes to `config.h` only. Use the `l` and `r` serial commands to find the device and work out its report layout. Pre-USB (serial/parallel) I/O boards won't work with this approach.

## Credits

- [TinyUSB](https://github.com/hathach/tinyusb) (MIT)
- [Pico PIO USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB) (MIT)
- [Arduino-Pico core](https://github.com/earlephilhower/arduino-pico) by Earle Philhower

Golden Tee, PowerPutt and Silver Strike are trademarks of Incredible Technologies, Inc. This project isn't affiliated with or endorsed by them.

## License

MIT, see [LICENSE](LICENSE).
