# Transition Forge for OBS

Design animated scene transitions in your browser, then use them in OBS as **native transitions**. No video files, no stinger.
Built by [Love Thy Nerd](https://lovethynerd.com).

Two parts:

* **Transition Forge** (`forge/`): a single-page designer with about 40 CSS/WAAPI transitions (wipes, shapes, themed, minimal, stroked text, tiled logos). It can also render a transparent `.webm`/`.mov` for OBS's Stinger transition.
* **Forge Transitions plugin** (`src/`): an OBS plugin. Every preset you save in the Forge shows up in OBS as `Forge: <name>`.

## Install (Windows)

1. Download **`TransitionForge-Setup-x.y.z.exe`** from the [latest release](../../releases/latest).
2. Close OBS, run the installer. It installs the plugin to `%ProgramData%\obs-studio\plugins\forge-transitions` and the designer to `%ProgramData%\LoveThyNerd\TransitionForge` (with its own portable Node.js, so nothing else is required).
3. Start OBS. In the log (Help > Log Files > View Current Log) you should see `Forge Transitions loaded`.

Windows may show a SmartScreen warning because the installer is not code-signed: choose *More info > Run anyway*.
Requires OBS 31 or newer (64-bit) with the built-in Browser source.

## Use

1. Open **Transition Forge** from the Start Menu and design a transition.
2. In the **OBS plugin** panel, enter a name and press **Save as OBS transition**. The plugin picks it up within a couple of seconds, no OBS restart needed.
3. In OBS: **Scene Transitions > +** and choose **Forge: your name**. (OBS has no public API for a plugin to add transitions to the dropdown itself, so each preset is added once with the + button.)

Saving the same name again updates the preset. The helper window does not need to stay open while you stream.

### Optional: video (stinger) export
Run **Install video export (optional)** from the Start Menu once (downloads Playwright + Chromium) and install ffmpeg (`winget install Gyan.FFmpeg`).

## Notes and limits

* Sync offset (transition properties): the overlay starts a frame or two after the cut logic. Adjust if the scene change feels early or late (default 60 ms).
* Studio Mode T-bar does not drive the animation (it is time-based).
* SVG stroke/fill alternation needs the helper's sidecar files; PNG logos use an outline filter fallback.
* Windows only for now. The plugin has not yet been tested on every OBS release; please open an issue with your OBS log if something fails.

## Build from source

Push a tag like `v0.1.0` (or run the **Build** workflow in the Actions tab). GitHub Actions builds the plugin against OBS 31.1.1, packages the Setup `.exe`, and attaches both to the release.
No Visual Studio needed locally.

```
git tag v0.1.0
git push origin v0.1.0
```

Run the designer without installing: `cd forge && node forge-server.mjs`.

## License

GPL-2.0-or-later (see `LICENSE`), matching OBS Studio's plugin template.
