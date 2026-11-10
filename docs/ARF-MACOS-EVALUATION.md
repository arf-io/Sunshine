# macOS remote desktop evaluation

Date: 2026-11-09. Status: source and environment evaluation; implementation and live Sunshine streaming validation remain pending.

## Decision

Use Sunshine and Moonlight as the streaming foundation. First validate the existing Command-key path and establish a measured streaming baseline. Build real bidirectional clipboard synchronization as an optional negotiated feature. Modernize macOS capture and add measured audio/video synchronization as separate changes, preserving compatibility with unmodified clients.

Do not promise “full macOS compatibility” as an unqualified property: supported OS versions, user sessions, keyboard layouts, display modes, permission states, and clipboard formats need explicit coverage. FileVault preboot, protected video, and OS-intercepted shortcuts are separate platform constraints.

## Forks and exact versions inspected

- [arf-io/Sunshine](https://github.com/arf-io/Sunshine), upstream `LizardByte/Sunshine`, commit `0ddc73d46395f3ba3571a8a56c7408c9a5f3b8f1`.
- [arf-io/moonlight-qt](https://github.com/arf-io/moonlight-qt), upstream `moonlight-stream/moonlight-qt`, commit `4113dd26be0907f2edf1805c7b9fd23766e17161`.
- Sunshine's pinned libvirtualhid: `9f4cac3fff75430c028ef8bb8ddc4459968949f8`.
- Sunshine's pinned moonlight-common-c: `e80a786afedeaccff63e839e68c8f3d8a057c13d`.
- Moonlight's pinned moonlight-common-c: `04053843c90c4268583f2496b997d54d59f03957`.

The two applications pin different transport-library revisions. Preserve those pins until a compatibility change is deliberately tested; do not move both to a shared latest revision casually. A timestamp/control protocol change may require a third fork of moonlight-common-c; that additional fork has not been created.

Local projects use bare repositories with `master`, `integration`, and `wt-macos-evaluation` worktrees under `/home/hedon/sunshine` and `/home/hedon/moonlight-qt`. `origin` is the requested GitHub fork and `upstream` is the original project. Source copies and relevant input/transport submodules are downloaded; optional Sunshine build dependencies are not all initialized.

## Actual host and network

SSH inspection of Slut-Garden reports Apple M1 Max (24 GPU cores), arm64, macOS 27.0.1 build 26A434, and an attached 1920x1200 60 Hz display. Xcode is selected and the installed SDK reports 27.2. These are observed host values, not a claim of certified upstream support. Homebrew has CMake 4.4.5, FFmpeg 9.0.2_1, OpenSSL 3.5.9 and Opus 1.6.1. No Sunshine executable/package was found in the checks performed. Linux has Qt 6.12.0, SDL 2.32.74, libavcodec 63.1.102 and Opus 1.6.1.

The preceding ZeroTier investigation established a DIRECT laptop/Mac peer path after stabilizing Slut-Garden's outbound UDP mapping on both OPNsense firewalls. Short UDP reverse tests at 20 Mbps with 1200- and 2600-byte payloads reported zero loss and no reordered datagrams. TCP reverse tests remained variable (one 20-second test averaged 51 Mbps). These are network measurements, not Sunshine performance or A/V sync measurements.

Start a live baseline at native 1920x1200, 60 fps, SDR, stereo, approximately 15–20 Mbps. Confirm VideoToolbox encode and hardware decoding from logs rather than assuming hardware acceleration. Compare H.264 and HEVC for text clarity, decode latency and frame drops. Match the 60 Hz display. Use Moonlight remote-desktop/absolute mouse mode and compare a local cursor against the encoded cursor; avoid displaying two cursors. Measure cursor response separately from whole-frame latency. Keep a bounded frame queue; smoothing should not accumulate seconds of stale frames. Maintain VNC as a fallback during development.

## Command-key forwarding: existing implementation, validate first

Moonlight `app/streaming/input/keyboard.cpp` maps `SDL_SCANCODE_LGUI` and `SDL_SCANCODE_RGUI` to virtual keys `0x5B` and `0x5C`. Both are deliberately suppressed unless `isSystemKeyCaptureActive()` is true. `app/streaming/input/input.cpp` checks capture preference, focus, active keyboard capture, and fullscreen policy. Therefore a disabled capture preference or a local compositor shortcut can look like a missing host feature.

Sunshine `src/platform/macos/input.cpp` delegates to libvirtualhid. At the pinned revision, `src/platform/macos/macos_backend.cpp` maps `0x5B` to `kVK_Command` and `0x5C` to `kVK_RightCommand`, with `kCGEventFlagMaskCommand` and device-specific left/right modifier masks. This is concrete support in the source, not an end-to-end pass.

The Sunshine getting-started note saying Command keys are not forwarded is inconsistent with these paths. Do not remove the note until client/platform tests establish its remaining scope. In particular, this user's client is Linux; macOS-client shortcut interception is a different case.

First test Super+C/V/X/A/Z, Shift+Command+Z, Command+Tab and Command+Space, left/right modifiers, held modifier during disconnect, focus loss, reconnect, ANSI/ISO layouts, and Option-produced characters. Test fullscreen and windowed modes on the actual Wayland compositor. Distinguish local key capture failures from network transport and host event injection with key traces that exclude typed contents. Release all held keys on focus loss/disconnect. Expose a per-host Super-to-Command/Control-to-Command preference only if requested; do not globally change Linux shortcuts.

## Clipboard: a new feature is required

The current Moonlight `KeyComboPasteText` in `keyboard.cpp` reads SDL clipboard text and calls `LiSendUtf8TextEvent`. It injects text into the focused application; it does not set the Mac pasteboard, receive a Mac copy, synchronize images, or transfer files. No bidirectional pasteboard implementation was found in the inspected application source.

Recommended first increment:

1. Negotiate an `arf.clipboard.v1` capability. Unmodified peers keep existing behavior.
2. Add a bounded text-only clipboard channel over the existing paired-client HTTPS service, or a separate authenticated channel explicitly tied to that same pairing and active streaming session. Do not use an unauthenticated ZeroTier listener or put clipboard data in URL query strings.
3. Sunshine uses `NSPasteboard.generalPasteboard` and `changeCount`; Moonlight uses `QClipboard` on its GUI thread. Marshal host Cocoa work to the appropriate application thread.
4. Use session generation, origin identifier, sequence and content digest to prevent echo loops and reject stale updates after reconnect. Make reads/writes cancellable and rate-limited; propose a 1 MiB UTF-8 payload ceiling for the initial version.
5. Provide explicit send/receive controls, an obvious active state, no clipboard contents in logs, and no transfer outside the active authorized session. Handle password-manager/concealed clipboard types deliberately rather than blindly mirroring everything.
6. Test both directions with multiline Unicode, emoji, empty text, rapid copies, large payload rejection, reconnect, permission changes, peer revocation, and an older stock client/server. Check Linux Wayland clipboard ownership and application focus behavior.

PNG and HTML/RTF are follow-on formats with negotiated MIME types and independent size limits. Finder file copy requires an actual authenticated file-transfer design; a file URL copied to another machine does not transfer the referenced file. Full clipboard compatibility cannot be claimed from text-only support.

A host-to-client HTTPS clipboard extension can initially avoid changing moonlight-common-c. Keep reads off the GUI thread and isolate this traffic from UDP media pacing. Reuse pairing authorization, not the administrator web UI password. Sunshine `src/nvhttp.cpp` contains paired-client certificate verification and the existing `/launch` and `/resume` HTTPS handlers.

## Audio/video sync and smoothness

Current host video capture in `src/platform/macos/av_video.m` uses `AVCaptureScreenInput`; `display.mm` moves CMSampleBuffer/CVPixelBuffer objects into the frame pipeline. The inspected macOS path does not populate `img_t::frame_timestamp` from the sample-buffer presentation time. `src/stream.cpp` can fall back to its rate-control clock when that timestamp is absent. This is a timing improvement candidate, not proof that it caused an observed lip-sync problem.

Native system audio capture already exists in `src/platform/macos/av_audio.mm` using Core Audio taps. A blank Audio Sink selects the native path according to current documentation. Its IOProc receives audio timestamps but the inspected callback moves samples into buffers rather than propagating a common audio/video presentation timeline. Audio RTP timestamps advance on their own packet-duration counter.

Moonlight audio playback and video pacing are separate paths. `app/streaming/audio/renderers/sdlaud.cpp` bounds queued audio (including 30 ms pending-audio and 50 ms SDL queue checks); `app/streaming/video/ffmpeg-renderers/pacer/pacer.cpp` separately controls video frame queues. These are useful latency controls, but do not by themselves demonstrate a shared presentation clock. The transport audio callback delivers compressed samples without a capture-time argument; rigorous shared-clock scheduling may require a negotiated protocol/library extension on both sides.

Implementation sequence:

- Instrument capture, encode, transport, decode, video presentation and audio-device queue delay. Collect p50/p95/p99 frame times, dropped frames, audio underruns and A/V offset. Avoid per-frame synchronous logging in real-time callbacks.
- Preserve native capture timestamps with an explicitly calibrated conversion to a common monotonic host epoch; do not reinterpret CMTime or mach host-time units as C++ steady-clock units.
- Prototype a ScreenCaptureKit backend behind a feature flag while preserving the CVPixelBuffer/VideoToolbox zero-copy path. Apple documents timestamped CMSampleBuffer delivery for video and audio. Preserve the existing Core Audio tap path as a fallback until mute, sample rate and device-change semantics pass.
- If measurements show drift, negotiate a media-clock mapping and use an audio playback clock with small bounded resampling correction and video drop/repeat scheduling. Never compare unrelated RTP epochs directly. Add a bounded manual offset as a diagnostic aid, not as the sole drift fix.
- Retain existing low-latency behavior for clients that do not negotiate synchronization support.

Validate with a local flash/click reference, a client-side recording capturing both screen and audio, and a 30-minute playback test. Proposed initial acceptance: median absolute A/V offset <=30 ms, p95 <=50 ms, no persistent drift >20 ms over 30 minutes, and <1% dropped frames on a controlled network at the baseline settings. These are engineering targets, not measured results. Test wired speakers/headphones first and Bluetooth separately because the output device can add substantial latency.

## macOS compatibility boundary and release matrix

Cover Apple Silicon first on the actual host OS, then explicitly supported additional macOS versions and Intel if desired. Test screen/system-audio and input permissions, permission revoke/regrant, stable signed application identity, display attach/detach, resolution/scaling changes, sleep/wake, session lock/unlock, fast-user switching and audio-device/rate changes. Confirm that capture restarts without a stuck last frame or held modifiers. Do not equate an SDK build with runtime compatibility.

Run within the intended logged-in user's session. An administrative daemon is not a substitute for GUI-session permissions. FileVault preboot and protected content remain outside a normal screen-streaming session; login-window control must be investigated as a separate requirement.

The application repositories are GPL-3.0. The current libvirtualhid library and its broker components have separate licensing; its README says macOS keyboard/mouse use CoreGraphics, while virtual gamepads require a separately licensed broker. The desktop/clipboard scope does not require adding a gamepad broker. Preserve notices and examine distribution terms before packaging bundled components.

## Prioritized work and validation status

| Priority | Deliverable | Completion evidence |
|---|---|---|
| P0 | Stock baseline on Linux -> Mac over DIRECT ZeroTier | Capture/encode/decode telemetry, 10-minute stream, Command-key matrix |
| P1 | Correct remaining key capture/mapping issues | Targeted input regression tests plus real compositor/Mac tests |
| P1 | Bidirectional UTF-8 clipboard | Auth/session/loop/limit tests, both-direction integration tests |
| P2 | Capture timestamps and ScreenCaptureKit backend | macOS SDK build plus capture/device-change tests |
| P2 | Negotiated shared-clock A/V mode if needed | Flash/click offset measurements and 30-minute drift run |
| P3 | PNG/rich text, broader OS/layout/display coverage | Explicit format and OS matrix; file transfer separately scoped |

Completed: GitHub forks verified, source downloaded, relevant submodules inspected, real host/SDK inventory collected, Linux qmake configuration succeeded. The Linux baseline build result is recorded separately in this report after completion. No macOS host binary has been built or installed, no Sunshine/Moonlight pairing or streaming session has been measured, and no feature implementation or full compatibility certification is claimed.

## References

- [Moonlight key forwarding at inspected commit](https://github.com/arf-io/moonlight-qt/blob/4113dd26be0907f2edf1805c7b9fd23766e17161/app/streaming/input/keyboard.cpp)
- [Moonlight key capture policy](https://github.com/arf-io/moonlight-qt/blob/4113dd26be0907f2edf1805c7b9fd23766e17161/app/streaming/input/input.cpp)
- [macOS Command mapping in pinned libvirtualhid](https://github.com/LizardByte/libvirtualhid/blob/9f4cac3fff75430c028ef8bb8ddc4459968949f8/src/platform/macos/macos_backend.cpp)
- [Sunshine capture backend](https://github.com/arf-io/Sunshine/blob/0ddc73d46395f3ba3571a8a56c7408c9a5f3b8f1/src/platform/macos/av_video.m)
- [Sunshine audio backend](https://github.com/arf-io/Sunshine/blob/0ddc73d46395f3ba3571a8a56c7408c9a5f3b8f1/src/platform/macos/av_audio.mm)
- [Sunshine paired HTTPS service](https://github.com/arf-io/Sunshine/blob/0ddc73d46395f3ba3571a8a56c7408c9a5f3b8f1/src/nvhttp.cpp)
- [Apple ScreenCaptureKit](https://developer.apple.com/documentation/ScreenCaptureKit)
- [Apple NSPasteboard](https://developer.apple.com/documentation/AppKit/NSPasteboard)

## Related upstream reports

- [Moonlight shared clipboard request #1103](https://github.com/moonlight-stream/moonlight-qt/issues/1103) remains open when checked. Its request includes files as well as text, reinforcing the need to separate clipboard formats from actual file transfer.
- [Sunshine macOS hosting latency report #5441](https://github.com/LizardByte/Sunshine/issues/5441) describes cursor lag on an M4 Mac with AVCaptureScreen. This is an unverified user report, not a reproduction on Slut-Garden or evidence that a particular API change fixes it. Include cursor/frame latency in the baseline rather than attributing every stall to ZeroTier.
