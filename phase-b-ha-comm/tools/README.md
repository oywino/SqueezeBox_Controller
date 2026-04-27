# Phase B Tools and Deploy Guide

This file is the cleaned operational reference for building, deploying, and testing the current Phase B application.

Legacy source material remains available at:

```text
\\NASF67175\Public\ubuntu\phase-b-ha-comm\tools
```

Do not edit the legacy source folder. The older files copied into this workspace are retained for traceability; this README is the normalized instruction set.

## Known Contexts

### Windows / Codex Shell

Purpose:

- Access the authoritative repository through UNC.
- SSH into the NAS.
- Start commands in the Ubuntu container through the NAS.

Repository path:

```powershell
\\NASF67175\Public\ubuntu\SqueezeBox_Controller
```

NAS SSH target:

```text
host: 192.168.1.6
user: oywin
```

Verified NAS hostname:

```text
NASF67175
```

### NAS Shell

Codex reaches the NAS non-interactively. In that mode, Docker is not on `PATH` by default, so commands must add the Container Station path first:

```sh
export PATH=/share/CACHEDEV1_DATA/.qpkg/container-station/bin:$PATH
```

Verified container:

```text
ubuntu
```

Use non-interactive container commands from Codex:

```sh
docker exec ubuntu bash -lc '<command>'
```

Manual PuTTY sessions may use:

```sh
docker exec -it ubuntu bash
```

### Ubuntu Container

Authoritative repository path inside the container:

```sh
/workspace/SqueezeBox_Controller
```

Current Phase B app path:

```sh
/workspace/SqueezeBox_Controller/phase-b-ha-comm/ha-remote
```

Current Phase B runtime binary:

```sh
ha-squeeze-remote-armv5
```

The previous test-phase binary name, `ha-remote-armv5`, is preserved for the last verified communication-test artifact and should not be overwritten by current Phase B builds.

Build scripts expect the Buildroot toolchain at:

```sh
/workspace/output/host/bin
```

### Squeezebox Device

Verified current controller:

```text
root@192.168.1.65
```

Do not assume any previous address is still canonical. The controller uses DHCP; confirm the active IP on the Jive `Remote Login` screen before deploy/run.

Remote runtime directory:

```sh
/mnt/storage/phase-a-lvgl
```

Verified runtime behavior:

- Short Home opens/closes the placeholder left menu.
- Long Home performs emergency Exit and restores stock Jive.
- On BAT, the screen sleeps after about 30 seconds idle.
- On AC, the screen stays awake past 30 seconds.
- Key, wheel, or accelerometer activity wakes the screen.

On the verified `192.168.1.65` controller, this directory was not present initially and had to be created before deploy.

## Squeezebox SSH Access

To connect to any Squeezebox Controller over SSH, first boot the stock Jive UI normally.

Hard precondition for Codex:

```text
Before every Squeezebox SSH login attempt, ask the user to confirm that the controller is powered on, awake, connected to WiFi, and has SSH enabled.
```

Codex cannot wake or power on the controller remotely if it is off or asleep.

From the Jive UI:

1. Confirm the controller is connected to WiFi.
2. Confirm it has received a valid DHCP IP address.
3. Open `Settings`.
4. Open `Advanced`.
5. Open `Remote Login`.
6. Enable `SSH` using the selection box.

The same `Remote Login` screen prints the controller's current IP address.

Default SSH credentials:

```text
username: root
password: 1234
```

The Squeezebox SSH server requires legacy algorithms. Example CLI login, assuming the current DHCP address is `192.168.1.65`:

```sh
ssh -oKexAlgorithms=+diffie-hellman-group1-sha1 \
    -oHostKeyAlgorithms=+ssh-rsa \
    -c aes128-cbc \
    -oMACs=+hmac-sha1 \
    root@192.168.1.65
```

The IP address can change when the DHCP lease changes. Always confirm the current IP on the Jive `Remote Login` screen before connecting.

On first login from a new client, SSH will probably report an unknown host key and ask:

```text
Are you sure you want to continue connecting
```

Answer with the requested fingerprint confirmation or type:

```text
yes
```

### Verified Codex Login Procedure

Verified current controller:

```text
IP: 192.168.1.65
hostname: SqueezeboxController
kernel: Linux 2.6.22-P7-gc7ac3ffd armv5tejl
```

Plain `plink` from Codex fails in batch mode because PuTTY refuses the weak `diffie-hellman-group1-sha1` key exchange unless the session crypto policy allows it.

The existing saved PuTTY session named `Squeezebox` has the required legacy crypto ordering:

```text
KEX starts with: dh-group1-sha1
HostKey starts with: rsa
```

That saved session currently may point at an older IP. Do not edit it permanently for automation. Instead, copy it to a temporary session, override only `HostName`, run the command, then delete the temporary session.

From the Windows / Codex PowerShell shell:

```powershell
$src = 'HKCU\Software\SimonTatham\PuTTY\Sessions\Squeezebox'
$tmp = 'HKCU\Software\SimonTatham\PuTTY\Sessions\CodexTempSqueezebox65'
$pw = '1234'

try {
  reg delete $tmp /f *> $null
  reg copy $src $tmp /s /f *> $null
  reg add $tmp /v HostName /t REG_SZ /d 192.168.1.65 /f *> $null

  & 'C:\Program Files\PuTTY\plink.exe' `
    -load CodexTempSqueezebox65 `
    -batch `
    -l root `
    -pw $pw `
    "printf 'squeezebox-ssh-ok\n'; hostname; uname -a; pwd"

  $code = $LASTEXITCODE
} finally {
  reg delete $tmp /f *> $null
}

exit $code
```

Expected successful output:

```text
squeezebox-ssh-ok
SqueezeboxController
Linux SqueezeboxController 2.6.22-P7-gc7ac3ffd #1 Fri Jan 10 23:40:56 PST 2014 armv5tejl GNU/Linux
/root
```

For future commands, replace only the final remote command string. Keep the temporary-session creation and cleanup wrapper unchanged unless the active Squeezebox IP changes.

## Approval Model

Use this sequence for each code iteration:

```text
Proposal -> approval -> code edit -> build -> deploy -> hardware test -> documentation proposal -> approval -> documentation edit -> tag/release proposal -> approval
```

No deploy, test run, documentation update, tag, or release should be performed without the relevant approval.

## Device Safety Boundaries

The normal Phase B workflow is low brick-risk when limited to building in the container, copying the app to `/mnt/storage/phase-a-lvgl`, stopping stock Jive, running the app, and restoring Jive or power-cycling.

Do not perform high-risk device operations without explicit review and approval:

- Do not write to `/dev/mtd*`, `/dev/mtdblock*`, bootloader, kernel, root filesystem, or init scripts.
- Do not replace stock `/usr/bin/jive`, `/etc/init.d/squeezeplay`, Dropbear/SSH files, or WiFi startup configuration.
- Do not change watchdog behavior or `/var/run/squeezeplay.pid` handling except through the approved stock UI helper.
- Do not fill persistent storage or install/replace system libraries and binaries.
- Do not run unbounded CPU, memory, or disk-write loops on the device.

Every deploy/test operation must have a recovery path: app `Exit`, SSH-based Jive restart, or physical power-cycle.

## Build

Run from inside the Ubuntu container:

```sh
cd /workspace/SqueezeBox_Controller/phase-b-ha-comm/ha-remote
./build_modules.sh
```

Expected result:

```text
ha-squeeze-remote-armv5
```

The binary is intentionally ignored by Git.

The last verified test-phase binary name, `ha-remote-armv5`, is preserved for history. Current Phase B MVP builds use `ha-squeeze-remote-armv5`.

## Deploy

Prerequisites:

- Squeezebox is powered on and reachable over SSH.
- The active Squeezebox IP is confirmed.
- The app is not currently running on the Squeezebox, because an active binary can block overwrite.
- `stockui-stop-hard.sh` exists on the Squeezebox in `/mnt/storage/phase-a-lvgl`.

Run from inside the Ubuntu container:

```sh
cd /workspace/SqueezeBox_Controller/phase-b-ha-comm/ha-remote
SB_A_HOST=root@<SQUEEZEBOX_IP> ./deploy_sb_a.sh
```

Deploy result to verify:

```text
/mnt/storage/phase-a-lvgl/ha-squeeze-remote-armv5
```

### Verified Deploy Fallback from Codex

The Ubuntu container deploy script can reach the Squeezebox, but OpenSSH inside the container cannot provide the `root/1234` password non-interactively. For Codex-driven deploys to the verified controller, use the Windows PuTTY toolchain and the temporary `Squeezebox` session copy described above.

Create the runtime directory first:

```powershell
<temporary PuTTY session wrapper>
plink -load CodexTempSqueezebox65 -batch -l root -pw 1234 `
  "mkdir -p /mnt/storage/phase-a-lvgl && ls -ld /mnt/storage/phase-a-lvgl"
```

Copy the binary and helper using SCP mode. SFTP mode fails because the Squeezebox does not provide `/usr/libexec/sftp-server`.

```powershell
pscp -scp -load CodexTempSqueezebox65 -batch -l root -pw 1234 `
  \\NASF67175\Public\ubuntu\SqueezeBox_Controller\phase-b-ha-comm\ha-remote\ha-squeeze-remote-armv5 `
  root@192.168.1.65:/mnt/storage/phase-a-lvgl/ha-squeeze-remote-armv5

pscp -scp -load CodexTempSqueezebox65 -batch -l root -pw 1234 `
  \\NASF67175\Public\ubuntu\SqueezeBox_Controller\phase-b-ha-comm\stockui-stop-hard.sh `
  root@192.168.1.65:/mnt/storage/phase-a-lvgl/stockui-stop-hard.sh
```

Then set executable permissions:

```powershell
<temporary PuTTY session wrapper>
plink -load CodexTempSqueezebox65 -batch -l root -pw 1234 `
  "chmod 755 /mnt/storage/phase-a-lvgl/ha-squeeze-remote-armv5 /mnt/storage/phase-a-lvgl/stockui-stop-hard.sh && ls -lh /mnt/storage/phase-a-lvgl/ha-squeeze-remote-armv5 /mnt/storage/phase-a-lvgl/stockui-stop-hard.sh"
```

## Attached Test Run

Use attached mode first for validation, because status output remains visible in the terminal.

Prerequisites:

- Deploy completed successfully.
- Squeezebox IP is confirmed.
- Home Assistant host is confirmed.
- Long-lived HA token is available either as `HA_TOKEN` or in `HA_LL_Token.txt`.

Run from inside the Ubuntu container:

```sh
cd /workspace/SqueezeBox_Controller/phase-b-ha-comm/ha-remote
SB_A_HOST=root@<SQUEEZEBOX_IP> HA_HOST=<HA_HOST_IP> ./run_sb_a.sh
```

If using an environment token:

```sh
cd /workspace/SqueezeBox_Controller/phase-b-ha-comm/ha-remote
export HA_TOKEN='<LONG_LIVED_ACCESS_TOKEN>'
SB_A_HOST=root@<SQUEEZEBOX_IP> HA_HOST=<HA_HOST_IP> ./run_sb_a.sh
```

Do not put real tokens in documentation.

Expected terminal sequence for the current one-shot HA session:

```text
HA: connecting...
HA: ws handshake...
HA: sending auth...
HA: auth_ok
HA: sending get_states...
HA: waiting states result...
HA: snapshot ok
HA: disconnected
```

Expected LCD behavior:

- Stock Jive is stopped.
- HA remote UI appears.
- The screen shows `Start HA Session` and `Exit`.
- Bottom-right power indicator shows `AC`, `AC+`, `BAT`, or `PWR?`.
- Wheel changes focus.
- Pressing `Start HA Session` starts another one-shot HA session.
- Pressing `Exit` shows the termination message and restores Jive.

## Detached Test Run

Use detached mode only after attached mode has been verified.

Detached mode expects the HA token file to exist on the Squeezebox:

```sh
/mnt/storage/phase-a-lvgl/HA_LL_Token.txt
```

Run from inside the Ubuntu container:

```sh
cd /workspace/SqueezeBox_Controller/phase-b-ha-comm/ha-remote
SB_A_HOST=root@<SQUEEZEBOX_IP> HA_HOST=<HA_HOST_IP> ./run_sb_a_detached.sh
```

Runtime output on the Squeezebox:

```sh
/tmp/ha-remote.log
```

Runtime PID on the Squeezebox:

```sh
/tmp/ha-remote.pid
```

### Verified Detached Run Result

Verified on hardware with controller `192.168.1.65`:

```text
/mnt/storage/phase-a-lvgl/HA_LL_Token.txt created
stockui-stop-hard.sh executed
ha-squeeze-remote-armv5 started detached
```

Observed process state:

```text
ha-squeeze-remote-armv5 running
dropbear still running
```

Initial runtime log:

```text
fb_init: 240x320 bpp=16 line_len=480 yoffset=320
HA: connecting...
HA: ws handshake...
HA: sending auth...
```

Hardware observation:

```text
Detached run succeeded.
Screen/UI worked as expected.
Manual Exit restored Jive before ha-remote-armv5 terminated.
```

## Manual Squeezebox Recovery

If the app exits through the UI, Jive should restart automatically.

If manual recovery is needed on the Squeezebox:

```sh
kill $(cat /var/run/squeezeplay.pid)
rm -f /var/run/squeezeplay.pid
```

The watchdog-safe stock UI stop helper is:

```sh
/mnt/storage/phase-a-lvgl/stockui-stop-hard.sh
```

Its purpose is to point `/var/run/squeezeplay.pid` at a harmless sleep process, then kill `jive` and `jive_alsa`.

## Battery and Cradle Telemetry

The current hardware notes identify these `jivectl` commands:

```sh
/usr/bin/jivectl 17
/usr/bin/jivectl 23
/usr/bin/jivectl 25
```

Meaning:

| Command      | Meaning           |
| ------------ | ----------------- |
| `jivectl 17` | Battery raw value |
| `jivectl 23` | AC state          |
| `jivectl 25` | Charging state    |

Observed battery raw range:

```text
807 = 0%
875 = 100%
```

## Snapshot and Tag Policy

Create snapshots and tags only after successful hardware verification.

Tag naming:

```text
v<major>.<minor>.<patch>-<short-description>-verified
```

Examples:

```text
v0.9.0-ha-connection-layer-verified
v0.9.1-ha-auth-failure-handling-verified
v0.10.0-state-cache-rate-limit-verified
```

GitHub releases should be created only for milestone tags, not every small verified increment.

Release notes should include:

- what changed
- exact build command used
- exact deploy command used
- exact test command used
- observed terminal output summary
- observed LCD behavior
- recovery result

## External Binary References

The following device-extracted Squeezebox binaries remain in the legacy tools source only and are not copied into this workspace:

| File                 | Size    | SHA256                                                             |
| -------------------- | -------:| ------------------------------------------------------------------ |
| `squeezebox/jive`    | 1866748 | `A76C3337D04B266D0BD7E1678568DFC77FCCACCD76770244193D5DE3EE95A0B3` |
| `squeezebox/jivectl` | 3752    | `A44646E3212E44237232E8985E230BEF98DAC80E8CC8CDAEDFF6BA1B6FA987E8` |

## Open Items to Reconcile

- Confirm the active Squeezebox SSH target IP and update scripts or invocation examples accordingly.
- Confirm the canonical Home Assistant host used for Phase B tests.
- Decide whether `run_sb_a.sh` should read `HA_LL_Token.txt` only from the workspace, from the Squeezebox, or both.
- Decide whether `stockui-stop-hard.sh` should be deployed automatically with the binary or managed manually.
