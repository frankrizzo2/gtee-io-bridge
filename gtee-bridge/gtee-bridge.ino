// Golden Tee LIVE I/O board -> USB mouse + keyboard bridge for Raspberry Pi Pico (RP2040).
//
// Core 1: USB HOST on GP10/GP11 (Pico-PIO-USB + TinyUSB host with hub support).
//         Talks only to the controls device 0344:03AB behind the board's hub (port 1) and reads EP 0x81.
// Core 0: USB DEVICE on the Pico's micro-USB: HID keyboard + mouse + CDC serial (debug).
//
// Build: Arduino, Earle Philhower RP2040 core, Tools > USB Stack = "Adafruit TinyUSB",
//        CPU Speed = 120 MHz (or 240 MHz), library "Pico PIO USB".
// Serial commands (any terminal, 115200): r = toggle raw report printing, s = status, h = help.

#include "pio_usb.h"
#include "Adafruit_TinyUSB.h"
#include "pico/util/queue.h"
#include "pico/sync.h"
#include "hardware/watchdog.h"

// Marker kept in a watchdog scratch register across our own stall reboot.
#define STALL_MAGIC 0x6754EE01u
#include "config.h"

// tuh_edpt_xfer() only reports completions when this is enabled; the RP2040 core leaves it off.
// Build with -DCFG_TUH_API_EDPT_XFER=1 (build.ps1 does this).
#if !CFG_TUH_API_EDPT_XFER
#error "Build with -DCFG_TUH_API_EDPT_XFER=1 (use build.ps1), otherwise endpoint transfers never complete"
#endif

// Debug counters added to our local copy of Pico-PIO-USB (pio_usb_host.c), for endpoints with max packet >= 32
// (the controls device's EP81 and EP02 only): [0] IN data ok, [1] IN DATA0/1 mismatch, [2] IN NAK, [3] IN stall,
// [4] IN error, [5] OUT ACK, [6] OUT NAK, [7] OUT stall.
volatile uint32_t gtee_dbg[8];   // stays 0 with stock Pico PIO USB 0.7.3 (counters were a local 0.7.2 patch)
// TX-path wait timeouts added to our patched pio_usb.c (0 = TX done flag, 1/2 = PIO program counter waits).
volatile uint32_t gtee_txto[4];   // stays 0: the pio_usb.c TX-timeout patch is NOT applied (see pio_usb.c.counter/.timeouts)

// ---------------------------------------------------------------- shared state (core1 -> core0)
struct LogLine { char text[96]; };
static queue_t log_q;                 // log messages from core 1
static queue_t report_q;              // raw 12-byte reports from core 1 (for printing only)
struct RawReport { uint8_t len; uint8_t data[12]; };

static critical_section_t state_lock;
static volatile int32_t  acc_dx = 0, acc_dy = 0;     // accumulated mouse movement
static volatile uint8_t  btn_bytes[12];              // latest report (for buttons)
static volatile bool     have_report = false;
static volatile uint32_t report_count = 0;
static volatile uint8_t  ctrl_addr = 0;              // 0 = controls device not mounted
static volatile bool     raw_print = false;
// Where core 1 last was: 1 = before USBHost.task, 2 = after it, 3 = in EP81 callback, 4 = callback done.
static volatile uint8_t  core1_where = 0;
static uint32_t          stall_reboots_seen = 0;      // 1 if the previous run ended in a core-1 stall reboot
static volatile bool     hid_output = HID_OUTPUT_AT_START;   // keyboard/mouse to the PC; 'o' toggles

static void logf_core1(const char* fmt, ...) {
  LogLine l; va_list ap; va_start(ap, fmt); vsnprintf(l.text, sizeof(l.text), fmt, ap); va_end(ap);
  queue_try_add(&log_q, &l);          // drop if full rather than block the USB host
}

// ---------------------------------------------------------------- USB device side (core 0)
enum { RID_KEYBOARD = 1, RID_MOUSE = 2 };
static const uint8_t desc_hid_report[] = {
  TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(RID_KEYBOARD)),
  TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(RID_MOUSE))
};
Adafruit_USBD_HID usb_hid;

// ---------------------------------------------------------------- USB host side (core 1)
Adafruit_USBH_Host USBHost;
static uint8_t rx_buf[64];

static void start_in_xfer(uint8_t daddr);

static volatile uint32_t xfer_done = 0, xfer_fail = 0, xfer_odd = 0;
static uint32_t in_fail_run = 0;   // consecutive failed EP81 transfers (core1 only)

static void in_xfer_cb(tuh_xfer_t* xfer) {
  core1_where = 3;
  xfer_done++;
  if (xfer->result != XFER_RESULT_SUCCESS) xfer_fail++;
  if (xfer->result == XFER_RESULT_SUCCESS && xfer->actual_len != 12) {
    if (xfer_odd++ < 8) {
      char hex[3 * 16 + 1] = { 0 }; int n = 0;
      for (uint32_t i = 0; i < xfer->actual_len && i < 16; i++) n += snprintf(hex + n, sizeof(hex) - n, "%02X ", rx_buf[i]);
      logf_core1("EP81 %lu-byte packet: %s", (unsigned long)xfer->actual_len, hex);
    }
  }
  if (xfer->result == XFER_RESULT_SUCCESS && xfer->actual_len >= 12) {
    // Note: TinyUSB passes buffer = NULL to edpt-xfer callbacks, so read our own buffer.
    const uint8_t* r = rx_buf;
    static uint8_t last_x = 0, last_y = 0; static bool primed = false;
    critical_section_enter_blocking(&state_lock);
    if (primed) {
      acc_dx += (int8_t)(uint8_t)(r[TRACKBALL_X_BYTE] - last_x);   // wrap-safe 8-bit delta
      acc_dy += (int8_t)(uint8_t)(r[TRACKBALL_Y_BYTE] - last_y);
    }
    for (int i = 0; i < 12; i++) btn_bytes[i] = r[i];
    have_report = true; report_count++;
    critical_section_exit(&state_lock);
    last_x = r[TRACKBALL_X_BYTE]; last_y = r[TRACKBALL_Y_BYTE]; primed = true;
    if (raw_print) { RawReport rr; rr.len = 12; memcpy(rr.data, r, 12); queue_try_add(&report_q, &rr); }
  } else if (xfer->result != XFER_RESULT_SUCCESS) {
    // A run of failures is normal while the cabinet powers off; log the first one only, not every retry.
    if (in_fail_run++ == 0) logf_core1("EP81 transfer result %d (further failures not logged)", xfer->result);
  }
  if (xfer->result == XFER_RESULT_SUCCESS && in_fail_run) {
    logf_core1("EP81 reads resumed after %lu failed transfers", (unsigned long)in_fail_run);
    in_fail_run = 0;
  }
  if (ctrl_addr == xfer->daddr) start_in_xfer(xfer->daddr);       // keep one transfer queued
  core1_where = 4;
}

static void start_in_xfer(uint8_t daddr) {
  tuh_xfer_t xfer = {};
  xfer.daddr = daddr; xfer.ep_addr = CTRL_EP_IN;
  xfer.buflen = sizeof(rx_buf); xfer.buffer = rx_buf;
  xfer.complete_cb = in_xfer_cb; xfer.user_data = 0;
  if (!tuh_edpt_xfer(&xfer)) logf_core1("EP81 xfer submit failed");
}

// Keep-alive: a copy of the game's 24-byte EP02 packet (counters + probable lamp bytes).
// Compiled in, but OFF until switched on with the serial command 'k' (or SEND_KEEPALIVE = 1).
static volatile bool keepalive_on = SEND_KEEPALIVE;
static volatile uint32_t ka_sent = 0, ka_fail = 0;
static uint8_t ka_buf[24] = { 0x1F,0x21,0x00,0x00,0x58,0x0C,0,0,0,0,0,0,0,0,0,0,0,0, 0xF0,0xF0,0xF0,0x00,0x00,0xF0 };
static volatile bool ka_busy = false;
static void ka_cb(tuh_xfer_t* x) { if (x->result == XFER_RESULT_SUCCESS) ka_sent++; else ka_fail++; ka_busy = false; }
static void send_keepalive(uint8_t daddr) {
  if (ka_busy) return;
  ka_buf[0]++; ka_buf[1] = (ka_buf[1] <= 0x20) ? 0x3F : ka_buf[1] - 1; ka_buf[3] ^= 1;
  tuh_xfer_t x = {}; x.daddr = daddr; x.ep_addr = CTRL_EP_OUT; x.buflen = 24; x.buffer = ka_buf; x.complete_cb = ka_cb;
  ka_busy = tuh_edpt_xfer(&x);
  if (!ka_busy) ka_fail++;
}

// ---- The game's start-up sequence, exactly as captured from the cabinet (boot1 window 026, boot2b window 008).
// Read-only vendor requests, then 40 04 (vendor OUT, no data). Nothing here is computed from board data.
static volatile bool do_game_init = false;
static volatile bool do_list = false;
static volatile bool do_clear_halt = false;

static void list_devices() {
  int n = 0;
  for (uint8_t a = 1; a <= CFG_TUH_DEVICE_MAX + CFG_TUH_HUB; a++) {
    if (tuh_mounted(a)) { uint16_t v = 0, p = 0; tuh_vid_pid_get(a, &v, &p); logf_core1("  addr %u: %04X:%04X", a, v, p); n++; }
  }
  logf_core1("%d device(s) mounted (hubs may not be listed)", n);
}

static bool ctrl_req(uint8_t daddr, uint8_t bmrt, uint8_t breq, uint16_t wv, uint16_t wi, uint16_t wl,
                     uint8_t* buf, uint32_t* got) {
  tusb_control_request_t req = {};
  req.bmRequestType = bmrt; req.bRequest = breq; req.wValue = wv; req.wIndex = wi; req.wLength = wl;
  tuh_xfer_t x = {};
  x.daddr = daddr; x.ep_addr = 0; x.setup = &req; x.buffer = buf; x.complete_cb = NULL; x.user_data = 0;
  bool ok = tuh_control_xfer(&x);                                   // blocking (complete_cb == NULL)
  if (got) *got = x.actual_len;
  return ok && x.result == XFER_RESULT_SUCCESS;
}

static void log_reply(const char* what, bool ok, const uint8_t* b, uint32_t n) {
  char hex[3 * 16 + 4] = { 0 }; int k = 0;
  for (uint32_t i = 0; i < n && i < 16; i++) k += snprintf(hex + k, sizeof(hex) - k, "%02X ", b[i]);
  if (n > 16) snprintf(hex + k, sizeof(hex) - k, "...");
  logf_core1("%s %s (%lu bytes) %s", what, ok ? "OK" : "FAILED", (unsigned long)n, hex);
}

static void run_game_init(uint8_t daddr) {
  static uint8_t buf[128]; uint32_t n; bool ok;
  logf_core1("--- sending the game's captured start-up sequence");
  ok = ctrl_req(daddr, 0xC0, 0x03, 0x0000, 0x0000, 0x22, buf, &n); log_reply("c0 03 (info)", ok, buf, n);
  for (uint16_t idx = 0x86; idx <= 0x99; idx++) {
    ok = ctrl_req(daddr, 0xC0, 0x05, 0x0001, idx, 2, buf, &n);
    char what[24]; snprintf(what, sizeof(what), "c0 05 idx %02X", idx); log_reply(what, ok, buf, n);
  }
  ok = ctrl_req(daddr, 0xC0, 0x0A, 0x0000, 0x0000, 0x1A, buf, &n); log_reply("c0 0a", ok, buf, n);
  ok = ctrl_req(daddr, 0xC0, 0x00, 0x0000, 0x0000, 0x7E, buf, &n); log_reply("c0 00", ok, buf, n);
  ok = ctrl_req(daddr, 0x40, 0x04, 0x0000, 0x0000, 0, NULL, &n);   log_reply("40 04 (start)", ok, buf, 0);
  logf_core1("--- sequence done; reports so far %lu", (unsigned long)report_count);
}

// Standard CLEAR_FEATURE(ENDPOINT_HALT) on both endpoints, as the game's driver did for the port-3 device
// (boot1 window 027: 02 01 0000 0081 / 02 01 0000 0001). Standard USB request, not vendor-specific.
static void run_clear_halt(uint8_t daddr) {
  uint32_t n; bool ok;
  ok = ctrl_req(daddr, 0x02, 0x01, 0x0000, CTRL_EP_IN, 0, NULL, &n);  log_reply("02 01 clear-halt EP81", ok, NULL, 0);
  ok = ctrl_req(daddr, 0x02, 0x01, 0x0000, CTRL_EP_OUT, 0, NULL, &n); log_reply("02 01 clear-halt EP02", ok, NULL, 0);
  logf_core1("--- clear-halt done; reports so far %lu", (unsigned long)report_count);
}

void tuh_mount_cb(uint8_t daddr) {
  uint16_t vid = 0, pid = 0; tuh_vid_pid_get(daddr, &vid, &pid);
  logf_core1("mounted addr %u  %04X:%04X", daddr, vid, pid);
  if (vid == CTRL_VID && pid == CTRL_PID) {
    static const uint8_t ep_in[7]  = { 7, TUSB_DESC_ENDPOINT, CTRL_EP_IN, TUSB_XFER_INTERRUPT, 64, 0, 1 };
    if (!tuh_edpt_open(daddr, (tusb_desc_endpoint_t const*)ep_in)) { logf_core1("EP81 open FAILED"); return; }
    static const uint8_t ep_out[7] = { 7, TUSB_DESC_ENDPOINT, CTRL_EP_OUT, TUSB_XFER_BULK, 32, 0, 0 };
    if (!tuh_edpt_open(daddr, (tusb_desc_endpoint_t const*)ep_out)) logf_core1("EP02 open FAILED");
    ctrl_addr = daddr;
    logf_core1("controls device ready, reading EP81");
    start_in_xfer(daddr);
  } else if (vid == HUB_VID && pid == HUB_PID) {
    logf_core1("board hub enumerated OK (hub descriptor accepted)");
  } else {
    logf_core1("other device %04X:%04X ignored", vid, pid);   // e.g. port-3 TUSB3410: never used
  }
}

void tuh_umount_cb(uint8_t daddr) {
  logf_core1("unmounted addr %u", daddr);
  if (daddr == ctrl_addr) {
    if (in_fail_run) logf_core1("  (%lu failed EP81 transfers before unmount)", (unsigned long)in_fail_run);
    in_fail_run = 0;
    ctrl_addr = 0;
    critical_section_enter_blocking(&state_lock); have_report = false; acc_dx = acc_dy = 0; critical_section_exit(&state_lock);
  }
}

void setup1() {
  // PIO-USB needs the system clock to be a multiple of 12 MHz (120 or 240 MHz).
  uint32_t cpu_hz = clock_get_hz(clk_sys);
  if (cpu_hz != 120000000UL && cpu_hz != 240000000UL) {
    logf_core1("ERROR: CPU clock %lu Hz; set Tools > CPU Speed to 120 or 240 MHz", (unsigned long)cpu_hz);
    while (true) delay(1000);
  }
  pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
  pio_cfg.pin_dp = HOST_PIN_DP;
  USBHost.configure_pio_usb(1, &pio_cfg);
  USBHost.begin(1);
  logf_core1("USB host started on GP%d/GP%d", HOST_PIN_DP, HOST_PIN_DP + 1);
}

static volatile uint32_t core1_loops = 0;
extern "C" uint32_t pio_usb_host_get_frame_number(void);

void loop1() {
  core1_loops++;
  core1_where = 1;
  USBHost.task();
  core1_where = 2;
  if (do_list) { do_list = false; list_devices(); }
  if (do_clear_halt) { do_clear_halt = false; if (ctrl_addr) run_clear_halt(ctrl_addr); else logf_core1("no controls device mounted"); }
  if (do_game_init) { do_game_init = false; if (ctrl_addr) run_game_init(ctrl_addr); else logf_core1("no controls device mounted"); }
  static uint32_t last_ka = 0;
  if (keepalive_on && ctrl_addr && millis() - last_ka >= 16) { last_ka = millis(); send_keepalive(ctrl_addr); }
}

// ---------------------------------------------------------------- core 0
void setup() {
  // Note whether the previous run ended in a core-1 stall reboot, then clear the marker.
  bool after_stall = (watchdog_hw->scratch[0] == STALL_MAGIC);
  uint32_t stall_where = watchdog_hw->scratch[1], stall_reports = watchdog_hw->scratch[2];
  watchdog_hw->scratch[0] = 0;
  stall_reboots_seen = after_stall ? 1 : 0;

  queue_init(&log_q, sizeof(LogLine), 64);
  if (after_stall) {
    LogLine l; snprintf(l.text, sizeof(l.text), "previous run: core1 stalled (where %lu, after %lu reports) -> auto reboot",
                        (unsigned long)stall_where, (unsigned long)stall_reports);
    queue_try_add(&log_q, &l);
  }
  queue_init(&report_q, sizeof(RawReport), 64);
  critical_section_init(&state_lock);

  if (!TinyUSBDevice.isInitialized()) TinyUSBDevice.begin(0);
  usb_hid.setPollInterval(1);
  usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
  usb_hid.setStringDescriptor("GTee IO Bridge");
  usb_hid.begin();
  if (TinyUSBDevice.mounted()) { TinyUSBDevice.detach(); delay(10); TinyUSBDevice.attach(); }

  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
#if PICO_HELP_PIN >= 0
  pinMode(PICO_HELP_PIN, INPUT_PULLUP);
#endif
#if PICO_OPTIONS_PIN >= 0
  pinMode(PICO_OPTIONS_PIN, INPUT_PULLUP);
#endif
}

// Debounced read of a button wired from a Pico pin to GND. Returns true while pressed.
static bool pico_button(int pin, uint32_t now, bool& state, bool& raw_last, uint32_t& raw_since) {
  if (pin < 0) return false;
  bool raw = digitalRead(pin) == LOW;
  if (raw != raw_last) { raw_last = raw; raw_since = now; }
  else if (raw != state && now - raw_since >= PICO_BUTTON_DEBOUNCE_MS) state = raw;
  return state;
}

// Reboots the Pico if core 1 (USB host) stops making progress. Called from core 0's loop.
static void check_core1_alive() {
  static uint32_t last_loops = 0, last_change = 0;
  uint32_t now = millis(), loops = core1_loops;
  if (loops != last_loops) { last_loops = loops; last_change = now; return; }
  if (last_change && now - last_change > CORE1_STALL_REBOOT_MS) {
    watchdog_hw->scratch[0] = STALL_MAGIC;
    watchdog_hw->scratch[1] = core1_where;
    watchdog_hw->scratch[2] = report_count;
    watchdog_reboot(0, 0, 10);                  // reboot in 10 ms (scratch registers survive)
    while (true) tight_loop_contents();
  }
}

static void print_help() {
  Serial.println("GTee IO Bridge. Commands: r = raw reports on/off, s = status, o = keyboard/mouse output on/off (starts OFF), l = log history + device list, g = send game start-up sequence, c = clear-halt both endpoints, k = keep-alive on/off, h = help");
}

void loop() {
#ifdef TINYUSB_NEED_POLLING_TASK
  TinyUSBDevice.task();
#endif
  check_core1_alive();
  // Drain log lines from core 1: print them and keep the last 64 (with time) for the 'l' command.
  static LogLine history[64]; static uint32_t hist_n = 0;
  LogLine l;
  while (queue_try_remove(&log_q, &l)) {
    LogLine h; snprintf(h.text, sizeof(h.text), "%7lu ms  %s", (unsigned long)millis(), l.text);
    history[hist_n % 64] = h; hist_n++;
    Serial.print("[host] "); Serial.println(h.text);
  }
  RawReport rr;
  while (queue_try_remove(&report_q, &rr)) {
    char s[48]; int n = 0;
    for (int i = 0; i < rr.len; i++) n += snprintf(s + n, sizeof(s) - n, "%02X ", rr.data[i]);
    Serial.println(s);
  }
  // Serial commands.
  while (Serial.available()) {
    int c = Serial.read();
    if (c == 'r') { raw_print = !raw_print; Serial.printf("raw reports %s\n", raw_print ? "ON" : "OFF"); }
    else if (c == 's') Serial.printf("controls addr %u, reports %lu, EP81 transfers done %lu (failed %lu, not-12-byte %lu), keep-alive %s sent %lu failed %lu\n",
                                     ctrl_addr, (unsigned long)report_count, (unsigned long)xfer_done, (unsigned long)xfer_fail,
                                     (unsigned long)xfer_odd, keepalive_on ? "ON" : "OFF", (unsigned long)ka_sent, (unsigned long)ka_fail),
                       Serial.printf("t=%lu ms core1 loops %lu where %u frame %lu, output %s | ", (unsigned long)millis(), (unsigned long)core1_loops,
                                     core1_where, (unsigned long)pio_usb_host_get_frame_number(), hid_output ? "ON" : "OFF"),
                       Serial.printf("pico Help pin %s, Options pin %s | ",
                                     PICO_HELP_PIN < 0 ? "off" : (digitalRead(PICO_HELP_PIN) == LOW ? "PRESSED" : "up"),
                                     PICO_OPTIONS_PIN < 0 ? "off" : (digitalRead(PICO_OPTIONS_PIN) == LOW ? "PRESSED" : "up")),
                       Serial.printf("txto %lu/%lu/%lu | ", (unsigned long)gtee_txto[0], (unsigned long)gtee_txto[1], (unsigned long)gtee_txto[2]),
                       Serial.printf("bus: IN data %lu, IN toggle-mismatch %lu, IN NAK %lu, IN stall %lu, IN error %lu | OUT ack %lu, OUT NAK %lu, OUT stall %lu\n",
                                     (unsigned long)gtee_dbg[0], (unsigned long)gtee_dbg[1], (unsigned long)gtee_dbg[2], (unsigned long)gtee_dbg[3],
                                     (unsigned long)gtee_dbg[4], (unsigned long)gtee_dbg[5], (unsigned long)gtee_dbg[6], (unsigned long)gtee_dbg[7]);
    else if (c == 'l') {
      uint32_t start = hist_n > 64 ? hist_n - 64 : 0;
      Serial.printf("--- log history (%lu lines)\n", (unsigned long)(hist_n - start));
      for (uint32_t i = start; i < hist_n; i++) Serial.println(history[i % 64].text);
      do_list = true;
    }
    else if (c == 'o') { hid_output = !hid_output; Serial.printf("keyboard/mouse output %s\n", hid_output ? "ON" : "OFF"); }
    else if (c == 'c') { do_clear_halt = true; Serial.println("clear-halt queued"); }
    else if (c == 'g') { do_game_init = true; Serial.println("game start-up sequence queued"); }
    else if (c == 'k') { keepalive_on = !keepalive_on; Serial.printf("keep-alive %s\n", keepalive_on ? "ON" : "OFF"); }
    else if (c == 'h') print_help();
  }

  // LED: blink = waiting for the board, solid = receiving reports.
  digitalWrite(LED_BUILTIN, ctrl_addr ? HIGH : ((millis() / 400) & 1));

  // Send HID reports (about every 2 ms when the host is ready).
  static uint32_t last_send = 0;
  if (millis() - last_send < 2) return;
  if (!hid_output || !TinyUSBDevice.mounted() || !have_report) return;
  if (TinyUSBDevice.suspended()) { TinyUSBDevice.remoteWakeup(); return; }

  int32_t dx, dy; uint8_t b[12];
  critical_section_enter_blocking(&state_lock);
  dx = acc_dx; dy = acc_dy;
  for (int i = 0; i < 12; i++) b[i] = btn_bytes[i];
  critical_section_exit(&state_lock);
  uint32_t now = millis();

  // Help/Options wired to the Pico: merge into the board's button byte (active low) so they behave
  // exactly like the board's own Help/Options, including the exit combo.
  static bool help_st = false, help_raw = false, opt_st = false, opt_raw = false;
  static uint32_t help_t = 0, opt_t = 0;
  if (pico_button(PICO_HELP_PIN, now, help_st, help_raw, help_t))   b[7] &= ~(1u << 5);
  if (pico_button(PICO_OPTIONS_PIN, now, opt_st, opt_raw, opt_t))   b[7] &= ~(1u << 6);

  // Exit combo: Help (byte 7 bit 5) + Options (byte 7 bit 6) held together for EXIT_COMBO_MS -> tap EXIT_COMBO_KEY once.
  static uint32_t combo_since = 0, esc_until = 0; static bool combo_fired = false;
  bool help_down = !(b[7] & (1u << 5)), opts_down = !(b[7] & (1u << 6));
  bool combo = help_down && opts_down;
  if (combo) {
    if (!combo_since) combo_since = now ? now : 1;
    if (!combo_fired && now - combo_since >= EXIT_COMBO_MS) { combo_fired = true; esc_until = now + EXIT_COMBO_TAP_MS; }
  } else { combo_since = 0; combo_fired = false; }

  // Keyboard: one key per pressed button (up to 6 at once). Help/Options keys are held back during the combo.
  uint8_t keys[6] = { 0 }; int k = 0;
  for (int i = 0; i < BUTTON_COUNT; i++) {
    const ButtonMap& m = BUTTONS[i];
    bool pressed = !(b[m.byte] & (1u << m.bit));          // active low
    if (combo && m.byte == 7 && (m.bit == 5 || m.bit == 6)) continue;
    if (pressed && m.key && k < 6) keys[k++] = m.key;
  }
  if ((int32_t)(esc_until - now) > 0 && k < 6) keys[k++] = EXIT_COMBO_KEY;
  static uint8_t last_keys[6] = { 0 };
  bool keys_changed = memcmp(keys, last_keys, 6) != 0;

  if (!usb_hid.ready()) return;
  if (keys_changed) {
    usb_hid.keyboardReport(RID_KEYBOARD, 0, keys);
    memcpy(last_keys, keys, 6);
    last_send = millis();
    return;                                                 // next report on a later pass
  }
  if (dx || dy) {
    int32_t mx = dx * MOUSE_SCALE * (MOUSE_INVERT_X ? -1 : 1);
    int32_t my = dy * MOUSE_SCALE * (MOUSE_INVERT_Y ? -1 : 1);
    mx = constrain(mx, -127, 127); my = constrain(my, -127, 127);
    usb_hid.mouseReport(RID_MOUSE, 0, (int8_t)mx, (int8_t)my, 0, 0);
    // Remove only what was sent (scaled back), keep any remainder for the next report.
    int32_t sent_x = mx / (MOUSE_SCALE * (MOUSE_INVERT_X ? -1 : 1));
    int32_t sent_y = my / (MOUSE_SCALE * (MOUSE_INVERT_Y ? -1 : 1));
    critical_section_enter_blocking(&state_lock); acc_dx -= sent_x; acc_dy -= sent_y; critical_section_exit(&state_lock);
    last_send = millis();
  }
}
