# Jive-Aligned Architecture Redesign

Purpose: define the Phase B runtime architecture needed to reach Jive-level input, UI, and audio responsiveness while preserving the established microservice discipline.

## Trigger

The current generated ALSA beep implementation proves that tuning sound playback inside the existing loop is the wrong level of fix. The user-visible problem is architectural:

- Input read, focus movement, and sound feedback are perceived as separate events.
- Audio feedback can still become choppy under scroll load.
- Responsiveness is below Jive even though the same Linux core is available.

## Jive Findings

Jive is not a single flat loop.

- `/usr/bin/jive` runs as the main UI process.
- `/etc/init.d/squeezeplay` starts Jive with:
  - `SDL_NOMOUSE=1`
  - `ALSA_CONFIG_PATH=/usr/share/alsa/alsa.conf`
  - `SQUEEZEPLAY_HOME=/etc/squeezeplay`
  - `LD_PRELOAD=/usr/lib/libspotify.so.6`
- `jiveBSP.so` reads `/dev/input/event%d` and pushes input into Jive's event queue.
- `Framework:eventLoop()` runs a timed UI frame loop.
- `Task.lua` prioritizes audio work ahead of lower-priority UI/network work.
- `NetworkThread.lua` isolates network socket waiting from UI work.
- `Menu.lua` / `ScrollWheel.lua` clamp controller wheel movement to one visible menu item at a time.
- Menu movement calls `playSound("CLICK")` only after selection actually changes.
- Sound is queued with `Sample:play()`, not played synchronously in the menu/input path.
- `jive_alsa` is a separate audio process using shared memory, ALSA mmap, and `sched_setscheduler`.

Conclusion: Jive feels responsive because input dispatch, UI frame timing, audio effect queuing, and blocking I/O are separated by clear priority boundaries.

## Required Phase B Architecture

The Phase B runtime should become an event-driven microservice system with one orchestrator and bounded subsystem ownership.

```text
Linux input devices
        |
        v
HAL input reader
        |
        v
Input event queue
        |
        v
Action mapper
        |
        +--> UI command queue --> UI/frame service --> framebuffer
        |
        +--> Audio effect queue --> audio service/process --> ALSA
        |
        +--> HA command queue --> HA session service --> Home Assistant
```

## Microservice Boundaries

### HAL

Owns:

- Linux device paths.
- `open`, `read`, `poll`, `ioctl(EVIOCGRAB)`.
- Raw power and WiFi telemetry adapters.

Does not own:

- Button meaning.
- UI focus.
- Sound.
- HA actions.

### Input Service

Owns:

- Non-blocking event intake from HAL.
- Normalized input events.
- Reusable short/long press detection.
- Wheel event normalization.

Does not own:

- UI object mutation.
- Audio playback.
- HA service calls.

### Action Mapper

Owns:

- Translating normalized input into semantic commands.
- Example: `menu_toggle`, `menu_move_down`, `primary_select`, `emergency_exit`.

Does not own:

- Drawing.
- Sound rendering.
- Network calls.

### UI Service

Owns:

- LVGL objects.
- Approved visual state.
- Frame timing.
- Applying queued UI commands.
- Redraw minimization.

Does not own:

- Raw input reads.
- Blocking shell calls.
- ALSA calls.
- HA WebSocket I/O.

### Audio Feedback Service

Owns:

- Effect queue.
- Effect selection.
- Audio backend lifecycle.
- Playback priority.

Does not own:

- Menu state.
- Input reads.
- UI redraw.

Target direction:

- Stop generated beep tuning as the long-term path.
- Reproduce or reuse Jive's effect-queue model.
- Use Jive's own `click.wav` and `select.wav` assets only through an approved audio backend.

### HA Session Service

Owns:

- Short HA WebSocket session lifecycle.
- LLAT auth.
- `get_states`.
- Later service-call transport.

Does not own:

- UI rendering.
- Card selection.
- Entity/card interpretation.

### State Cache Service

Owns:

- Configured entity state cache.
- Change detection.
- Rate limiting.

Does not own:

- Network transport.
- Drawing.

## Event Ordering Requirement

For one wheel detent that moves menu focus:

1. HAL receives raw wheel event.
2. Input service emits one normalized wheel event.
3. Action mapper emits one menu move command.
4. UI service applies focus change.
5. Audio service is queued with click feedback.
6. UI frame flush and audio playback proceed independently.

The user must experience this as one event. No input path may wait for audio or network work.

## Main Loop Requirement

The orchestrator must not perform blocking work directly. It should:

- Poll or receive input quickly.
- Drain bounded command queues.
- Run LVGL frame handling on a predictable cadence.
- Let audio and HA work run outside the UI/input path.
- Log slow tasks when they exceed a defined threshold.

## Immediate Design Correction

Before adding more HA MVP features, the next implementation should introduce the missing event boundary:

- Input should enqueue semantic commands instead of directly mutating UI and starting audio.
- UI should consume UI commands.
- Audio should consume audio commands.
- Menu movement should queue UI focus change and audio click as sibling effects from one semantic action.

This is the smallest useful correction toward Jive's architecture without yet reverse engineering the full `jive_alsa` shared-memory protocol.

## Approval Gates

No code should be changed until the user approves the next single step.

Recommended next step:

Create a minimal internal event queue and route Home/menu/wheel actions through it, with no visual change and no audio backend change.

