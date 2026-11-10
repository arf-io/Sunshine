# ARF desktop streaming evaluation and implementation

Status: implemented and built, 2026-11-09. This is an experimental fork, not a claim of complete macOS compatibility. The Linux client is paired with Slut-Garden over ZeroTier IPv6. Live screen, keyboard, clipboard and physical audio/video validation await macOS Screen Recording and Accessibility consent.

## What changed

* Native ScreenCaptureKit capture on supported macOS versions, with a bounded three-frame capture queue, native presentation timestamps, explicit idle shutdown and legacy AVFoundation fallback.
* Explicit, paired TLS clipboard sharing during an active session. Unicode text, HTML, RTF and PNG are supported. File transfer is not implemented. Concealed/transient/file clipboards are skipped; content is never logged. Both ends must opt in.
* A negotiated capture-time audio/video extension: the host timestamps audio and video on one monotonic timeline; the client follows estimated audible audio playback, holds early video and drops obsolete frames. Stock hosts and clients retain their original wire format. The transport extension lives in arf-io/moonlight-common-c, branch feat/arf-avsync.
* Client settings with native Qt styling, keyboard focus help and accessible names. Existing system-key capture remains the mechanism for forwarding Super/Command; live macOS shortcut behavior still needs validation.

## Protocol and limits

Clipboard uses POST /arf/clipboard over the existing paired HTTPS connection. The server requires clipboard enablement, an enabled paired certificate and a running stream belonging to that certificate. Each request resolves its actual TLS peer identity, rather than a global last-handshake identity. Payloads are MIME-keyed canonical base64, with SHA-256 content digests, optimistic expected-digest writes and known-digest replies. Initial synchronization establishes a baseline without overwriting either clipboard. Polling is asynchronous every 250 ms and unchanged replies omit content. Limits: 4 MiB decoded aggregate, 6 MiB JSON, 16,384-pixel image dimensions and 64 million pixels. Conflicts and later local copies cannot be overwritten by stale replies.

The launch/resume query requests arfavsync=1; only a supporting macOS host echoes arfAvSync=1. Negotiated audio carries ARF1 plus an unsigned 64-bit big-endian first-sample host-monotonic timestamp in microseconds before Opus, inside the existing encryption/FEC path. Video uses the same clock in 90 kHz RTP units. The client unwraps RTP around the audio anchor and estimates audible time from SDL queued audio plus device buffering. Video holds are bounded to 150 ms, stale clocks fall back, and the combined pacing/render queue is bounded to 12 frames in this opt-in mode. Device latency estimates and synthetic drift tests do not prove physical speaker/display synchronization, especially over Bluetooth.

## Installed evaluation

Slut-Garden: /Applications/ARF Sunshine.app, signed with the existing development identity. The original /Applications/Sunshine.app remains available. A user LaunchAgent at ~/Library/LaunchAgents/io.arf.sunshine.desktop.plist opens the ARF app at login. Configuration enables clipboard and ScreenCaptureKit, IPv4/IPv6 listening, and disables UPnP. Private administrator credentials are at ~/.config/sunshine/arf-desktop-admin.json on that Mac; do not publish this file.

Laptop: ~/.local/bin/arf-moonlight and a desktop entry named Slut-Garden — ARF Desktop. The target is fc2e:a9de:d5e:3a2d:7fc::1, application Desktop. The evaluation profile is 1920x1200 at 60 fps, 20 Mbps, 1200-byte packets, absolute mouse, frame pacing, VSync, system-key capture, clipboard and negotiated A/V synchronization. Hardware HEVC decoding was detected on the laptop. This is a starting profile, not a measured optimum.

macOS must allow ARF Sunshine in Privacy & Security → Screen & System Audio Recording and Accessibility, and approve Local Network/system audio prompts when presented. Quit and reopen the app after changes. SSH cannot grant those OS consent controls. Keep the existing VNC route until live streaming works.

## Verification

* Linux Sunshine app and tests built; 13 focused audio-packet/capture-clock/clipboard/TLS/lifetime tests and 28 keyboard/authorization regressions passed.
* Native Apple Silicon Sunshine app and tests built. The corresponding 16 focused tests (including isolated native pasteboards), 28 keyboard/authorization tests, and 28 native capture lifecycle/audio tests passed. Synthetic audio unit fixtures no longer unnecessarily require screen capture access.
* Linux Moonlight client built and opened on the real desktop. Settings controls and accessible descriptions were exercised. The Qt clipboard/clock suite reports 15 passes, including TLS pinning, disabled host, conflict/stale-reply handling, unchanged-image polling, RTP wrap and simulated 30-minute audio-device drift.
* The transport parser passed its CMake test and AddressSanitizer/UndefinedBehaviorSanitizer checks.
* Pairing and authenticated application listing succeeded over the target IPv6 address. ZeroTier reported DIRECT at the last observation. Neither observation measures live streaming quality.

Not yet verified: live ScreenCaptureKit/VideoToolbox encoding, end-to-end dropped frames/latency, actual Command shortcuts, real bidirectional rich clipboard transfer, physical A/V offset, macOS-client builds, Intel Macs, multiple monitors/HDR, all OS versions, login/lock screens, and file clipboard transfer. No universal compatibility claim is made.

## Rebuild and test

Sunshine follows upstream CMake instructions, with BUILD_TESTS=ON and macOS frameworks ScreenCaptureKit and ImageIO. Format using uv sync --locked then uv run --locked lb-update-clang-format before compiling. Run test_sunshine with focused filters AudioPacketSessionTest.*, CaptureClockTest.*, DesktopClipboardTest.*, HTTPS peer/lifetime tests and the native ScreenCaptureKitLifecycleTest/AVAudio tests; use --gtest_list_tests for exact suite names.

Moonlight follows upstream qmake6/make instructions after git submodule update --init --recursive. The client requires this fork's pinned transport submodule. Tests: qmake6 tests/desktopclipboard/desktopclipboard.pro in a separate build directory, make, then QT_QPA_PLATFORM=offscreen ./test-desktopclipboard (use the generated TARGET name if different). The transport's standalone CMake option ARF_BUILD_TESTS=ON enables ctest.

## Rollback

Stop ARF Sunshine, unload its user LaunchAgent, and open the original Sunshine.app. Pre-install configuration is saved privately under /Users/hedon/arf-sunshine/backups/20261109-171727/config; restore it only with the server stopped and after preserving any later changes. Disable the two new client options to retain stock clipboard and timing behavior. The existing VNC connections and access were retained throughout deployment.

## Desktop cursor defaults

Moonlight now defaults showlocalcursor=true. In remote desktop mouse mode this shows an immediate local pointer; the Input Settings checkbox can disable it and Ctrl+Alt+Shift+C remains a session override. Sunshine macOS defaults macos_capture_cursor=disabled for both ScreenCaptureKit and AVFoundation. The Advanced checkbox can restore capture after a server restart. This does not transmit remote cursor shape or hotspot metadata: the local arrow stays an arrow over remote text fields or resize handles. Relative/game mouse mode keeps SDL relative cursor handling. Other host platforms are unaffected.

The installed ARF app and future macOS fork builds use the distinct bundle identity io.arf.sunshine.desktop and display name ARF Sunshine. This avoids relaunching the original app and keeps its privacy grants separate. Enable Screen Recording and Accessibility for the exact ARF app after switching identities. The GUI LaunchAgent executes the ARF binary directly.
