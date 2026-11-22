# Wangan Browser (Android WebView shell)

This is a **thin Android WebView wrapper** for the public site
<https://wmmtd.xyz/>. It is **not** the Super3DX+ game APK, not a native port,
and not an offline copy of the game.

It contains only this project's Java shell, a launcher icon, and Android
resources. There is **no native library, no game dump, no converted `data/`
folder, no save data, and no copy of the site's WASM/JS assets inside the APK**.
The app loads `https://wmmtd.xyz/` over the network at runtime, so it needs an
internet connection and the site must remain available.

## What it does

- Opens `https://wmmtd.xyz/` fullscreen.
- Locks the activity to landscape (`sensorLandscape`).
- Enables JavaScript, DOM storage, and hardware acceleration.
- Keeps the screen on while the app is focused.
- Uses immersive sticky fullscreen flags.
- Supports the site's standard WebView `WebChromeClient` fullscreen videos.
- Sends links outside `wmmtd.xyz` to the browser instead of loading them inside the app.
- Uses package id `com.wmmt.webview`, so it installs beside the existing
  `com.wmmt.port` game APK.
- Uses the app label **Wangan Browser** and the same custom launcher icon artwork
  as the other builds, when `data/icon/Icon-76@2x.png` is present. If it is not,
  the build script generates a plain placeholder icon.

The Android back button goes back in the WebView history, exits a WebView
fullscreen video first, and then leaves the app.

## Build

Requirements:

- Python 3
- Android SDK with build-tools 35 or newer and an installed Android platform
- JDK 17 or newer
- The existing debug keystore at `.super3dx/android/debug.keystore` if you want
  updates to keep the same Android signing identity

On Windows, using the SDK/JDK paths already on this machine:

```powershell
python platform\webview\build_apk.py `
  --sdk "C:\Super3DXPlus\.super3dx\android-sdk" `
  --jdk "C:\Program Files\Microsoft\jdk-17.0.21.21-hotspot" `
  --out "C:\Super3DXPlus\WanganBrowser-WebView.apk" `
  --keystore "C:\Super3DXPlus\.super3dx\android\debug.keystore"
```

On Linux/macOS:

```bash
python3 platform/webview/build_apk.py \
  --sdk "$ANDROID_HOME" \
  --jdk "$JAVA_HOME" \
  --out WanganBrowser-WebView.apk
```

The build runs `javac` → `d8` → `aapt2 link` → add `classes.dex` → `zipalign`
→ `apksigner`. It does not use Gradle and does not download or embed game data.

## Verify

Example verification commands:

```powershell
$bt = "C:\Super3DXPlus\.super3dx\android-sdk\build-tools\35.0.0"
& "$bt\aapt2.exe" dump badging WanganBrowser-WebView.apk
& "$bt\apksigner.bat" verify --print-certs WanganBrowser-WebView.apk
```

Expected details:

- package: `com.wmmt.webview`
- label: `Wangan Browser`
- launchable activity: `com.wmmt.webview.MainActivity`
- signer certificate SHA-256:
  `f0b7caed880aa21076b34daf06a2bcdfc1e00365fa66da7cf9fab423577ae1c2`
- APK entries: manifest/resources/`classes.dex` only; no `lib/`, `assets/`,
  `.so`, data, or game files

## Notes / limits

- This is a browser shell, so anything that depends on the site's server,
  browser codecs, SharedArrayBuffer/cross-origin isolation, WebGL, workers, or
  the Gamepad API depends on `wmmtd.xyz` and on Android WebView's behavior.
  It has been built and statically verified here, but not tested on a device.
- No source or data was copied from `wmmtd.xyz`. Only the public URL is opened.
- Because this app uses the same debug keystore as the other Android build,
  keep that keystore safe. Losing it means Android will refuse to install a
  later update over the same package name.
- This project does not grant rights to the game, the site, or any game data.
  Use your own legally obtained game data and respect the site owner's terms.
