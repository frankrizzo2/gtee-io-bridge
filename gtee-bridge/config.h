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

// --- Buttons -> keys ---
// Byte 7 bits (and byte 8 bit 7), active low. Keys match GTee Clubhouse's DEFAULT "Global Game Controls"
// (from the user's screenshots, 2026-11-20), so no remapping is needed in Clubhouse.
// Clubhouse's "Test" has no default key: the bridge sends F2, bind Test -> F2 once in Clubhouse.
// Key codes are USB HID usage IDs (HID_KEY_* names from TinyUSB). 0 = send nothing.
struct ButtonMap { uint8_t byte; uint8_t bit; uint8_t key; const char* name; };
#define BUTTON_COUNT 8
static const ButtonMap BUTTONS[BUTTON_COUNT] = {
  { 7, 0, HID_KEY_ENTER,       "Start/Replay" },   // Clubhouse Start   = Enter
  { 7, 1, HID_KEY_ARROW_LEFT,  "Left"         },   // Clubhouse Left    = Left
  { 7, 2, HID_KEY_ARROW_RIGHT, "Right"        },   // Clubhouse Right   = Right
  { 7, 3, HID_KEY_ARROW_UP,    "Fly-by"       },   // Clubhouse FlyBy   = Up
  { 7, 4, HID_KEY_ARROW_DOWN,  "Spin"         },   // Clubhouse Spin    = Down
  { 7, 5, HID_KEY_W,           "Help"         },   // Clubhouse Help    = W
  { 7, 6, HID_KEY_Q,           "Options"      },   // Clubhouse Options = Q
  { 8, 7, HID_KEY_F2,          "Service/Test" },   // Clubhouse Test    = not assigned by default -> bind to F2
};

// --- Optional keep-alive (OFF). Only enable if the cold-start test shows the board needs it,
// --- and after checking it is a lamp/watchdog packet, not a security response.
#define SEND_KEEPALIVE     0
