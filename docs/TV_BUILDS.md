# Vector Strike on TVs: Samsung (Tizen) and Android TV

Both builds are the same C++ game as the Batocera build. The Bluetooth Wii remote driver is left out of both (`VS_NO_WII`); you play with the TV remote, gamepads, or a keyboard.

## TV remote controls (both TVs)
- Arrows fly, or move through menus.
- OK fires, or selects.
- BACK pauses in a game, or goes back in menus.
- Play/Pause pauses (Android).

On the Samsung start screen, BACK quits the app. On Android TV, BACK is trapped (`SDL_HINT_ANDROID_TRAP_BACK_BUTTON`); quit from the title menu.

Gamepads: press A to join, up to 4 players in split screen. Enter / OK now also fires in a game on every build (it did nothing there before).

## Samsung QN75Q80TAFXZA (2020 Q80T, Tizen 5.5)
Samsung TVs run Tizen, not Android, and third-party apps on them must be web apps. So the Samsung build is the WebAssembly version packed as a Tizen web app.

**Building** (`./build_tizen.sh`, needs emcc, see WEB_BUILD.md for the cloud recipe):
- The output is `tizen/app/`, a ready Tizen Studio web project: `config.xml`, `index.html`, `icon.png`, and `vectorstrike.js`.
- Everything is in one `vectorstrike.js` of about 6 MB, with the wasm and the model pack inlined (`-sSINGLE_FILE --embed-file`). The TV's web engine then never has to fetch `.wasm` / `.data` files.
- `tizen/VectorStrike-unsigned.zip` holds the same files (not committed).

**The page** (`tizen/src/index.html`):
- Passes the remote's Back key (keyCode 10009) to the game through the exported `vs_tvkey()`.
- Quits the app with `tizen.application...exit()` when Back is pressed on the start screen.
- Shows "PRESS OK TO START".

App id `VStrk00001.VectorStrike`, profile `tv-samsung`.

**Tested** in headless Chromium from `file://`, like a packaged app: it loads, OK starts and selects, the arrows fly, OK fires, and Back goes back. **Not yet run on the TV.**

**Installing on the TV** (signing needs Chris's own Samsung certificate, so this part happens on his Mac):
1. Install **Tizen Studio** with the **TV Extensions** and the **Samsung Certificate Extension** (Package Manager > Extension SDK).
2. On the TV, turn on **Developer Mode**:
   - Open the Apps panel and type `1 2 3 4 5` on the remote.
   - Switch Developer Mode on and enter the Mac's IP address.
   - Restart the TV.
3. In Tizen Studio's **Device Manager**, add the TV by its IP and connect.
4. In the **Certificate Manager**, create a **Samsung** certificate profile (TV type). Sign in with a Samsung account, and when it asks for devices, pick the connected TV so its DUID is included.
5. Import `tizen/app` as a Tizen web project, or point the CLI at it, then:
   `tizen package -t wgt -s <profile> -- tizen/app` and `tizen install -n VectorStrike.wgt -t <tv name>`.
   Or in Studio: right-click the project > Run As > Tizen Web Application.
6. The app then appears in the TV's apps. Re-install after each new build.

Untested on the TV itself:
- whether the Q80T's web engine gives SDL's gamepad code (the W3C Gamepad API) your Xbox / Switch Pro pads;
- frame rate on the TV's chip. If it stutters, use Options > RESOLUTION 720p.

## Android TV (Chromecast with Google TV, Shield, Fire TV, etc.)
**Built by GitHub Actions**, because the Android SDK/NDK can't be downloaded in the Claude sessions. The build is `.github/workflows/android-tv.yml`:
- It runs on every push that touches `src/`, `android/`, `modelpack/` or the workflow, or by hand from the Actions tab.
- It fetches SDL 2.30.12, copies SDL's Java (`org.libsdl.app`) and the model pack in, and runs `gradle assembleDebug` (AGP 8.2.2, Gradle 8.7, JDK 17, compileSdk 34, minSdk 21, arm64-v8a + armeabi-v7a).
- It uploads the artifact **VectorStrike-AndroidTV** (the .apk).
- The Claude proxy blocks the Actions API, so a session can't watch the run. If it fails, Chris pastes the error.

**Project** (`android/`):
- `app/jni/CMakeLists.txt` builds SDL2 plus `libmain.so` from `src/main.cpp`.
- `VectorStrikeActivity` extends `SDLActivity`.
- The manifest has `LEANBACK_LAUNCHER`, a banner, touchscreen and gamepad not required, and landscape.
- On first run the game copies `assets/modelpack/*.vsm` to the app's internal storage (`SDL_AndroidGetInternalStoragePath`). Settings and scores are kept there too.

**Installing:**
- Turn on Developer options on the TV device: Settings > About > press Build 7 times. Allow USB/network debugging, then `adb connect <ip>` and `adb install VectorStrike-debug.apk`.
- Or put the .apk on a USB stick / cloud drive and install it with a file manager that allows unknown apps.
