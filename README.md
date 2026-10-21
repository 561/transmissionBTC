# Transmission BitTorrent Client for Android: Android TV / Google TV fork

A fork of [AndreyPavlenko/transmissionbtc](https://github.com/AndreyPavlenko/transmissionbtc) tuned for one job: **torrenting on a TV box straight to a USB flash drive, without making the TV unusable.**

Built and tested on a Google TV Streamer (Android 14, 32-bit ARM) downloading to an exFAT Kingston flash drive. Stock Transmission on that setup freezes the UI, stalls the web interface for 20-30 seconds at a time and downloads at ~1 MB/s. This fork fixes the causes instead of papering over them.

## What this fork adds

### Downloads only when the TV is idle
* New setting **"Download only when idle (screensaver or screen off)"**.
* Downloading starts when the screensaver or screen-off begins and pauses within seconds when someone uses the TV, so playback and navigation never compete with the flash drive.
* Manual override from the web UI: **Download now / Pause / Auto**.

### Web UI: status badge and file deletion
* A badge in the header (before the theme switcher) shows whether the TV is downloading or paused, and why (idle, busy, user, forced).
* Buttons to force downloading, pause, or return to automatic mode, no waiting for the idle timer.
* A **delete** button in the files pager: removes the selected files from the drive and marks them as not wanted.
* Reload watchdog: the web UI auto-refresh no longer dies silently after one failed request.

### Lag fixes for slow USB flash drives (exFAT / FUSE)
* **No preallocation.** Android's exFAT FUSE layer zero-fills when a file is extended, which blocks the drive for minutes on multi-GB files.
* **Dirty-page throttle.** A 100 ms monitor reads `Dirty + Writeback` from `/proc/meminfo`. At 32 MB (configurable with `TRANSMISSION_MAX_DIRTY_MB`) downloading is throttled immediately inside `Bandwidth::clamp`, and released below half of that. This keeps the kernel write queue short, so the engine and web UI never wait on the drive.
* **Piece window** to keep downloads concentrated instead of scattered across the file.
* The engine changes live in [`patches/transmission-engine.patch`](patches/transmission-engine.patch), applied on top of Transmission 3.00.

### Reliability
* **Peer watchdog:** if a downloading torrent has no peers for a minute, it is re-announced to its trackers (and again at most once a minute). Transmission by default only re-announces every 30-60 minutes.
* **Settings are saved on every change.** Upstream writes `settings.json` only on a clean stop, so anything changed through the web UI (port, cache) was lost when Android killed the app or it was reinstalled.
* Fixed a stray `switch` fall-through in the native RPC callback.

### Control API (port 9093)
A tiny HTTP server for scripts and the web UI. It has no authentication, so keep it on your LAN.

| Request | Effect |
|---|---|
| `GET /idle/status` | `{"running","idleOnly","idle","suspended","override","reason"}` |
| `POST /idle/run` | download now, until the TV next changes idle state |
| `POST /idle/pause` | pause regardless of idle state |
| `POST /idle/auto` | back to idle-based control (also forces a state recalculation) |
| `POST /file/delete?path=...` | delete a file (and its `.part`) inside the download directory only |

### Measured on the target setup
| | Stock | This fork |
|---|---|---|
| Sustained download to exFAT flash | ~0.3-1 MB/s with stalls | 1.2-3.5 MB/s |
| Web UI response while downloading | 10-30 s freezes | 65-100 ms typical |
| TV usable during download | no | yes (pauses when you use it) |

Numbers come from one device and one swarm; yours will differ.

## Install

There are no prebuilt APKs, because release builds need your own signing key. Build from source (see below). The fork installs next to the original app as `com.ap.transmission.btc.fork`.

To start the daemon on a TV without a keyboard you can use ADB, for example:

    adb connect <tv-ip>:5555
    adb install -r build/outputs/apk/release/transmissionbtc-release.apk
    adb shell monkey -p com.ap.transmission.btc.fork -c android.intent.category.LAUNCHER 1

The web UI is then at `http://<tv-ip>:9091/`.

## Credits and license

Original application by [Andrey Pavlenko](https://github.com/AndreyPavlenko), built on the [Transmission](https://transmissionbt.com/) engine and [transmission-web-control](https://github.com/ronggang/transmission-web-control). Licensed under the GNU GPL v2, same as upstream.

---

# Upstream README

## Transmission BitTorrent Client for Android
[<img alt="Get it on Google Play" height="60" src="https://play.google.com/intl/en_us/badges/images/generic/en_badge_web_generic.png">](https://play.google.com/store/apps/details?id=com.ap.transmission.btc)
[<img alt="Get it at IzzyOnDroid" height="60" src="https://gitlab.com/IzzyOnDroid/repo/-/raw/master/assets/IzzyOnDroid.png">](https://apt.izzysoft.de/packages/com.ap.transmission.btc)

## About (upstream)
This application is a port of the Transmission daemon for Android complemented with the following features: 

* Manage downloads directly from the application
* Multiple watch/download directories
* Support for HTTP(S)/SOCKS proxy
* WiFi/Ethernet mode to save on mobile data
* Keep CPU/WiFi awake to complete all downloads before the device goes to sleep
* Sequential download allows playing media files while downloading 
* Open .torrent files or torrent/magnet URLs and stream the selected files to a media player
* Builtin UPnP MediaServer. Download media files to a phone/tablet/TV-box and watch on a TV or another UPnP compatible media player connected to the same network
* M3U playlists for all torrents/folders containing audio/video files. To get the playlist URL - long click on the play icon

## Building

* Download the latest Android NDK from https://developer.android.com/ndk/downloads/
* Download the latest Android SDK or Android Studio from https://developer.android.com/studio/
* Extract the downloaded archives
* Export the environment variable ANDROID_NDK_ROOT pointing to the NDK directory

### Build dependencies

    $ git clone https://github.com/AndreyPavlenko/android-build.git
    $ export ANDROID_BUILD_ROOT="$PWD/android-build"
    $ cd "$ANDROID_BUILD_ROOT/packages/transmission"
    $ ./build.sh all

### Build the apks
    $ git clone https://github.com/AndreyPavlenko/transmissionbtc.git

Open the local.properties file in a text editor and enter the valid paths to ndk.dir, sdk.dir, depends and keystore, set the keystore properties.

    $ cd transmissionbtc
    $ gradle cleanAll assembleRelease
