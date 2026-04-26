# Project Rules (Source of Truth)

These rules override chat memory. If there is any conflict, this file wins.

## Change discipline

- One change per iteration. Test/verify before next change.
- This includes fixes/corrections: still one change at a time.
- After each verified change: update CHANGELOG + create snapshot.
- When UI output changes, describe exactly what will be visible before running.
- Never provide code or doc snippets as diff files. Always handover complete files.
- Never modify any code file without first requesting an upload of the existing current working version of the same file.

## Squeezebox constraints

- No new tools/packages installed on Squeezebox unless mandatory.
- Avoid heavy/continuous activity that drains battery (prefer short WS sessions; reconnect on activity).

## Squeezebox file transfer

Before every Squeezebox SSH/SCP attempt, confirm with the user that the controller is powered on, awake, connected to WiFi, and has SSH enabled.

Use the current procedure in `phase-b-ha-comm/tools/README.md`. The currently verified controller is `root@192.168.1.65`, but the address is DHCP-assigned and must be confirmed from the Jive Remote Login screen before deploy/run.

For raw OpenSSH/SCP clients, the Squeezebox requires legacy-compatible options:

scp -O \
  -c aes128-cbc \
  -oKexAlgorithms=+diffie-hellman-group1-sha1 \
  -oHostKeyAlgorithms=+ssh-rsa \
  -oPubkeyAcceptedAlgorithms=+ssh-rsa \
  -oMACs=+hmac-sha1 \
  ha-remote-armv5 \
  root@<SQUEEZEBOX_IP>:/mnt/storage/phase-a-lvgl/

## Snapshot procedure (mandatory after “Verified”)

- Pick SNAP name: `vX.Y.Z-<short-description>-verified`
- Create snapshot folder under `snapshots/`
- RSYNC workspace into it (excluding `snapshots/`)
- Add/append a CHANGELOG entry with:
  - what changed
  - what was verified (device + observed result)

## Notes

- If line endings break tools (e.g., patch/python shebang issues), normalize in the build container and document the fix in CHANGELOG.

### Module scope contracts

→ `ha_ws.c: session orchestrator only`.

#### Definition of `ha_ws.c` responsibility:

`ha_ws.c` is responsible only for:

1. Owning a single one-shot HA WebSocket session lifecycle
   
   - Open TCP/WS connection to HA
   
   - Perform LLAT authentication
   
   - Send `get_states` (and only that, for now)
   
   - Receive the full response
   
   - Cleanly disconnect

2. Returning a session result to callers
   
   - Success/failure status (enum or similar)
   
   - Optional raw payload pointer/length for the `get_states` JSON

3. Emitting minimal, backend-style status signals
   
   - Simple callbacks or status flags like “connecting”, “auth failed”, “recv completed”, “disconnected”
   
   - No direct UI or LVGL calls (no labels, no message boxes)

`ha_ws.c` must NOT:

- Parse or interpret JSON into entities/cards

- Parse configuration files or LLAT tokens

- Own or update any LVGL objects or UI text

- Contain generic crypto primitives (SHA1, Base64) beyond what is strictly unavoidable right now

- Implement retries, backoff policies, or higher-level session scheduling

Rule going forward:

- No new responsibilities are added to `ha_ws.c`.

- Any new behavior related to UI, config, card logic, or crypto goes elsewhere.

- No files may be edited unless a known, full comparison is made against the currently running version of the same file.

- No functional, visual or logical modifications are allowed without explicit advance approval by me, or contractually agreed.

- Changes inside `ha_ws.c` are limited to: bug fixes, internal cleanup, or reducing its surface area.
