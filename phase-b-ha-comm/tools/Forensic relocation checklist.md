## 📑 Relocation Checklist for Monolithic `main.c`

### Framebuffer

- **Lines ~1400–1470**:
  
  - Code: `open("/dev/fb0")`, `ioctl(FBIOGET_VSCREENINFO/FBIOGET_FSCREENINFO)`, `mmap`, set `g_fb0/g_fb1/g_line_len/g_w/g_h`.
  
  - **Purpose:** Initialize framebuffer, map memory, set resolution.
  
  - **New home:** `fb.c` → `fb_init()`.

- **Lines ~1650–1670**:
  
  - Code: `munmap(g_fb_map, g_fb_map_len)`, `close(g_fb_fd)`, reset globals.
  
  - **Purpose:** Teardown framebuffer.
  
  - **New home:** `fb.c` → `fb_deinit()`.

- **Lines ~1100–1150**:
  
  - Code: `fb_flush_cb()` implementation (copy LVGL draw buffer into both framebuffers).
  
  - **Purpose:** LVGL flush callback.
  
  - **New home:** `fb.c` → `fb_flush_cb()`.

- **Lines ~1200–1220**:
  
  - Code: `lcd_wake()` (`system("/usr/bin/jivectl 11")`).
  
  - **Purpose:** Wake LCD panel.
  
  - **New home:** `fb.c` → `lcd_wake()`.

### Input

- **Lines ~1150–1200**:
  
  - Code: `drain_input()` loop (`read(fd, struct input_event)`, update `g_enc_diff/g_btn_pressed`, extend deadline).
  
  - **Purpose:** Process input events.
  
  - **New home:** `input.c` → static `drain_input()`.

- **Lines ~1220–1260**:
  
  - Code: `indev_encoder_read()` (calls `drain_input()`, reports encoder diff + button state).
  
  - **Purpose:** LVGL input device callback.
  
  - **New home:** `input.c` → `indev_encoder_read()`.

- **Lines ~1470–1520**:
  
  - Code: `open("/dev/input/event1/2/3")`, set O_NONBLOCK, EVIOCGRAB, warnings.
  
  - **Purpose:** Input initialization.
  
  - **New home:** `input.c` → `input_init()`.

- **Lines ~1630–1650**:
  
  - Code: `ioctl(... EVIOCGRAB, 0)`, `close(fds)`.
  
  - **Purpose:** Input teardown.
  
  - **New home:** `input.c` → `input_deinit()`.

### HA WebSocket / Session

- **Lines ~200–400**:
  
  - Code: SHA1 + Base64 helpers (`sha1_init/update/final`, `b64enc`).
  
  - **Purpose:** WebSocket handshake key generation.
  
  - **New home:** `ha_ws.c` → static helpers.

- **Lines ~400–700**:
  
  - Code: WebSocket framing (`ws_send_frame_text`, `ws_send_pong`, `ws_recv_text_sb`, `tcp_connect_host`).
  
  - **Purpose:** WebSocket protocol implementation.
  
  - **New home:** `ha_ws.c` → static helpers.

- **Lines ~700–950**:
  
  - Code: `ha_session_connect_and_auth()` (connect, handshake, auth_required/auth_ok).
  
  - **Purpose:** Establish HA WebSocket session.
  
  - **New home:** `ha_ws.c` → static `ha_session_connect_and_auth()`.

- **Lines ~950–1150**:
  
  - Code: `ha_session_start()` (send get_states, subscribe state_changed, set deadline).
  
  - **Purpose:** Start HA session.
  
  - **New home:** `ha_ws.c` → `ha_session_start()`.

- **Lines ~1150–1180**:
  
  - Code: `ha_session_process_msg()` (increment counter, update last_entity, refresh UI).
  
  - **Purpose:** Process HA events.
  
  - **New home:** `ha_ws.c` → static `ha_session_process_msg()`.

- **Lines ~1180–1200**:
  
  - Code: `ha_poll_timer_cb()` (deadline check, non‑blocking poll, recv, process msg).
  
  - **Purpose:** LVGL timer callback for HA polling.
  
  - **New home:** `ha_ws.c` → `ha_poll_timer_cb()`.

- **Lines ~1260–1280**:
  
  - Code: `ha_session_close()` (close fd, reset state).
  
  - **Purpose:** Close HA session.
  
  - **New home:** `ha_ws.c` → `ha_session_close()`.

### UI

- **Lines ~1280–1350**:
  
  - Code: `btn_probe_cb()` (start HA session), `btn_exit_cb()` (set exit flag).
  
  - **Purpose:** Button callbacks.
  
  - **New home:** `ui.c` → static callbacks.

- **Lines ~1350–1400**:
  
  - Code: LVGL screen creation: background, power indicator label, Start/Exit buttons, focus border styling, event callbacks, group focus.
  
  - **Purpose:** Build UI.
  
  - **New home:** `ui.c` → `ui_init()`.

- **Lines ~1400–1420**:
  
  - Code: `pwr_indicator_update()` (read jivectl values, set label text).
  
  - **Purpose:** Update AC/BAT indicator.
  
  - **New home:** `ui.c` → `pwr_indicator_update()`.

- **Lines ~1420–1440**:
  
  - Code: `pwr_indicator_timer_cb()` (calls update).
  
  - **Purpose:** LVGL timer callback.
  
  - **New home:** `ui.c` → `pwr_indicator_timer_cb()`.

- **Lines ~1440–1460**:
  
  - Code: `keep_visible()` (invalidate screen).
  
  - **Purpose:** Keep LVGL refreshing.
  
  - **New home:** `ui.c` → `keep_visible()`.

- **Lines ~1460–1470**:
  
  - Code: `ui_should_exit()` (return flag).
  
  - **Purpose:** Exit signal.
  
  - **New home:** `ui.c` → `ui_should_exit()`.

- **Lines ~1470–1480**:
  
  - Code: `ui_show_exit_screen()` (currently no‑op in monolithic).
  
  - **Purpose:** Placeholder for exit screen.
  
  - **New home:** `ui.c` → `ui_show_exit_screen()`.

### Stock UI Restart

- **Lines ~1280–1300**:
  
  - Code: `read_pidfile_int()` (parse pidfile).
  
  - **Purpose:** Helper to read pidfile.
  
  - **New home:** `stockui.c` → static `read_pidfile_int()`.

- **Lines ~1300–1330**:
  
  - Code: `stockui_restart_via_pidfile()` (kill pid, unlink file).
  
  - **Purpose:** Restart stock UI.
  
  - **New home:** `stockui.c` → `stockui_restart_via_pidfile()`.

### Main Orchestration

- **Lines ~1500–end (~1700)**:
  
  - Code: `main()` function:
    
    - Seed RNG
    
    - Call `fb_init`, `input_init`, `lcd_wake`
    
    - Init LVGL, register display + indev drivers
    
    - Create group, call `ui_init`, set `ui_set_on_start`
    
    - Create HA poll timer
    
    - Main loop (`lv_tick_inc`, `lv_timer_handler`)
    
    - Shutdown sequence (`ha_session_close`, `input_deinit`, `ui_show_exit_screen`, `fb_deinit`, `stockui_restart_via_pidfile`)
  
  - **Purpose:** Orchestration only.
  
  - **New home:** stays in new `main.c`.
