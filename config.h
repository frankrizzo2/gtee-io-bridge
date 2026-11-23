// ===== Golden Tee I/O board bridge: settings =====
#pragma once
#include <stdint.h>

// --- Pins (same tap wiring as the sniffer) ---
#define HOST_PIN_DP        10      // board D+ (green). D- (white) must be on GP11.

// --- Board identity (from captures) ---
#define HUB_VID            0x0344  // "IT USB I/O Board1" built-in hub
#define HUB_PID            0x33AA
#define CTRL_VID           0x0344  // controls device behind hub port 1
#define CTRL_PID           0x03AB
#define CTRL_EP_IN         0x81    // 12-byte input reports, interrupt, 64 B max
#define CTRL_EP_OUT        0x02    // 24-byte game packets (only used if SEND_KEEPALIVE)

// --- Trackball -> mouse ---
#define TRACKBALL_X_BYTE   5
#define TRACKBALL_Y_BYTE   4
#define MOUSE_SCALE        1       // multiply trackball counts (1, 2, 3 ...)
#define MOUSE_INVERT_X     0       // set to 1 if left/right is backwards
#define MOUSE_INVERT_Y     1       // set to 1 if up/down is backwards (confirmed backwards on the bench, 2026-11-20)

// --- Exit combo: hold Help + Options together for EXIT_COMBO_MS -> one tap of EXIT_COMBO_KEY
// --- (Clubhouse "Exit to Clubhouse" = Escape). While both are held, their own keys (W, Q) are not sent.
#define EXIT_COMBO_MS      4000
#define EXIT_COMBO_KEY     HID_KEY_ESCAPE
#define EXIT_COMBO_TAP_MS  100

// --- Optional extra buttons wired straight to the Pico (for cabinets whose harness doesn't bring
// --- Help/Options to the I/O board, e.g. a converted Silver Strike or PowerPutt pedestal).
// --- Wire each button between the pin and any Pico GND. Internal pull-ups are used, so no resistors.
// --- 3.3 V logic only: the button must switch to ground and carry nothing else (no lamp voltage).
// --- A press here acts exactly like the same button on the I/O board (same key, works in the exit combo).
// --- Set a pin to -1 to disable it.
#define PICO_HELP_PIN      2       // GP2 = physical pin 4
#define PICO_OPTIONS_PIN   3       // GP3 = physical pin 5   (GND is physical pin 3, right next to them)
#define PICO_BUTTON_DEBOUNCE_MS 10

// --- Keyboard/mouse output to the PC at power-up (1 = on). Serial command 'o' toggles it.
#define HID_OUTPUT_AT_START 1

// --- Safety net: if the USB host core (core 1) stops making progress for this long, reboot the Pico.
#define CORE1_STALL_REBOOT_MS 1000

// --- Buttons -> actions ---
// Byte 7 bits (and byte 8 bit 7), active low.
// These are only the FACTORY DEFAULTS. The mapping can be changed at run time over the serial port
// (use gtee-mapper.ps1, the GUI) and is saved in the Pico's flash, so no rebuild is needed.
// Clubhouse's "Test" has no default key: the bridge sends F2, bind Test -> F2 once in Clubhouse.
//
// Action types:
//   ACT_NONE                 send nothing
//   ACT_KEY,   HID_KEY_*     keyboard key (USB HID usage ID; 0xE0..0xE7 = Ctrl/Shift/Alt/GUI modifiers)
//   ACT_MOUSE, MOUSE_BTN_*   mouse button (1 = left, 2 = right, 4 = middle)
//   ACT_MEDIA, MEDIA_*       media / consumer key (volume up/down, mute, play/pause)
enum { ACT_NONE = 0, ACT_KEY = 1, ACT_MOUSE = 2, ACT_MEDIA = 3 };
#define MOUSE_BTN_LEFT     1
#define MOUSE_BTN_RIGHT    2
#define MOUSE_BTN_MIDDLE   4
#define MEDIA_VOLUME_UP    0x00E9
#define MEDIA_VOLUME_DOWN  0x00EA
#define MEDIA_MUTE         0x00E2
#define MEDIA_PLAY_PAUSE   0x00CD

struct ButtonInput { uint8_t byte; uint8_t bit; const char* name; };
struct Action      { uint8_t type; uint8_t pad; uint16_t code; };
#define BUTTON_COUNT 8
static const ButtonInput BUTTON_INPUTS[BUTTON_COUNT] = {
  { 7, 0, "Start/Replay" },
  { 7, 1, "Left"         },
  { 7, 2, "Right"        },
  { 7, 3, "Fly-by"       },
  { 7, 4, "Spin"         },
  { 7, 5, "Help"         },
  { 7, 6, "Options"      },
  { 8, 7, "Service/Test" },
};
static const Action DEFAULT_ACTIONS[BUTTON_COUNT] = {
  { ACT_KEY,   0, HID_KEY_ENTER      },   // Start/Replay -> Enter
  { ACT_MOUSE, 0, MOUSE_BTN_LEFT     },   // Left         -> left mouse click
  { ACT_MOUSE, 0, MOUSE_BTN_RIGHT    },   // Right        -> right mouse click
  { ACT_KEY,   0, HID_KEY_ARROW_UP   },   // Fly-by       -> Up
  { ACT_KEY,   0, HID_KEY_ARROW_DOWN },   // Spin         -> Down
  { ACT_KEY,   0, HID_KEY_W          },   // Help         -> W
  { ACT_KEY,   0, HID_KEY_Q          },   // Options      -> Q
  { ACT_KEY,   0, HID_KEY_F2         },   // Service/Test -> F2
};

// Run-time mapping as stored in flash (layout only; the values come from the defaults here or from the GUI).
struct RuntimeConfig {
  uint32_t magic; uint16_t version; uint16_t size;
  Action   act[BUTTON_COUNT];
  uint8_t  vol_en, vol_mod, vol_down, vol_up;
  uint8_t  mouse_scale, mouse_inv_x, mouse_inv_y, pad;
  uint32_t sum;
};

// --- Volume combo (Windows volume via the media-key "consumer control" HID report, no PC software needed) ---
// Hold the modifier button and press the down/up button. Values are indexes into BUTTON_INPUTS.
// While the modifier is held, the down/up buttons' normal actions are not sent, and once a volume step
// has been sent the modifier's own action is released for the rest of that hold.
// Holding down/up repeats the volume step. All of this is also changeable from the GUI.
#define VOLUME_COMBO_ENABLED   1
#define VOLUME_COMBO_MODIFIER  3       // Fly-by
#define VOLUME_COMBO_DOWN      1       // Left
#define VOLUME_COMBO_UP        2       // Right
#define VOLUME_REPEAT_DELAY_MS 400
#define VOLUME_REPEAT_MS       120
#define MEDIA_TAP_MS           30      // how long each media-key press is held

// --- Optional keep-alive (OFF). Only enable if the cold-start test shows the board needs it,
// --- and after checking it is a lamp/watchdog packet, not a security response.
#define SEND_KEEPALIVE     0
