# Threaded Runtime Architecture

Purpose: define the real multitasking architecture required for Phase B before further implementation.

## Design Principle

Input, UI, audio, and HA network work must not share one timing path.

The main failure mode to avoid is:

```text
read input -> update UI -> play sound -> poll HA -> draw
```

That design makes every subsystem sensitive to delays in every other subsystem.

The target model is:

```text
input thread  -> input/action queue -> UI thread
                                  \-> audio thread/process
                                  \-> HA worker
```

## Threads

### Main Thread

Owns:

- Startup order.
- Shutdown order.
- Signal/exit coordination.
- Starting and stopping subsystem threads.

Does not own:

- Input polling loop.
- LVGL frame loop.
- ALSA playback.
- HA socket waits.

### Input Thread

Owns:

- Blocking `poll()` on HAL input file descriptors.
- Immediate timestamping of raw input.
- Normalizing raw events into semantic input events.
- Short/long press state.
- Wheel direction and detent normalization.

Output:

- `INPUT_HOME_SHORT`
- `INPUT_HOME_LONG`
- `INPUT_MENU_WHEEL_UP`
- `INPUT_MENU_WHEEL_DOWN`
- `INPUT_SELECT_PRESS`
- Later: playback, volume, add, previous, next, accelerometer wake

Forbidden:

- LVGL calls.
- Framebuffer writes.
- ALSA calls.
- HA calls.
- Shell commands.

### UI Thread

Owns:

- `lv_tick_inc()`
- `lv_timer_handler()`
- All LVGL objects.
- Framebuffer flushing.
- Menu state.
- Card focus state.
- Approved visual output.

Consumes:

- UI commands generated from input/action events.
- State-cache update notifications.
- Power/WiFi status snapshots.

Produces:

- Audio feedback requests when UI state actually changes.
- HA service requests when user action requires HA.
- Exit request to main thread.

Forbidden:

- Blocking input reads.
- ALSA setup/playback.
- HA socket waits.
- Direct shell telemetry polling.

### Audio Feedback Thread Or Process

Owns:

- Audio effect request queue.
- Audio backend.
- Effect priority and coalescing.

Consumes:

- `AUDIO_CLICK`
- `AUDIO_SELECT`
- `AUDIO_BUMP`

Target backend:

- Long term: Jive-style effect queue via `jive_alsa` shared-memory protocol or equivalent separate audio engine.
- Temporary backend may exist only as an isolated backend behind this service.

Forbidden:

- UI mutation.
- Input reads.
- HA work.

### HA Worker Thread

Owns:

- HA WebSocket session lifecycle.
- LLAT auth.
- `get_states`.
- Later service calls.
- Reconnect/backoff timing.

Consumes:

- `HA_REFRESH_STATES`
- `HA_CALL_SERVICE`

Produces:

- State-cache updates.
- Action success/failure events.

Forbidden:

- LVGL calls.
- Input reads.
- Audio playback.

### Status Cache Thread

Owns:

- Slow power/WiFi polling.
- Cached status snapshots.

Forbidden:

- UI mutation.
- Input blocking.

## Queues

Use bounded queues. Overflow policy must be explicit.

### Input To UI Queue

Carries semantic UI commands.

Overflow:

- Drop repeated wheel events beyond the latest direction.
- Never drop Home long/emergency Exit.

### UI To Audio Queue

Carries sound requests.

Overflow:

- Coalesce repeated `AUDIO_CLICK`.
- Keep latest select/bump request.

### UI To HA Queue

Carries HA service and refresh requests.

Overflow:

- Reject with visible/logged busy state.
- Do not block UI.

### HA To UI Queue

Carries result/status events.

Overflow:

- Keep latest status per entity where possible.
- Log dropped events.

## Timing

Input:

- Blocks in `poll()` until input arrives.
- Event timestamp happens before any command dispatch.

UI:

- Fixed cadence target.
- No blocking operation inside frame path.
- Slow operation threshold logged.

Audio:

- Playback must not run on input or UI thread.
- Audio engine must stay ready during bursts or use Jive's effect queue model.

HA:

- Activity driven.
- No UI/input wait on WebSocket.

## Menu Movement Rule

For one accepted wheel movement:

1. Input thread emits one direction event.
2. UI thread applies at most one row movement.
3. UI thread requests click audio only if selection changed.
4. Audio thread plays or coalesces click independently.

This mirrors the Jive rule that wheel speed must not skip visible rows in the controller menu.

## Current Code Assessment

The current runtime still has these issues:

- Input is drained from LVGL's input callback, so input timing depends on UI frame cadence.
- Audio is threaded, but still uses our temporary ALSA backend and is not equivalent to Jive's effect engine.
- HA polling is timer-driven from the UI setup path.
- UI and input still have direct callback coupling.

## Implementation Order

Implement one boundary at a time.

1. Move input polling into a dedicated input thread.
2. Keep UI visuals unchanged while UI consumes input commands.
3. Keep audio backend unchanged initially, but route requests only through audio service.
4. Move HA trigger/result flow out of UI timer path.
5. Replace temporary audio backend with Jive-style effect engine when protocol is known.

## First Implementation Step

Create a dedicated input thread that owns `hal_poll_input()` blocking waits and emits semantic input commands to the UI thread.

No visual change. No audio backend change. No HA behavior change.

