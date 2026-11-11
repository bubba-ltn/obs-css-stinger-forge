# OBS CSS Stinger Forge

CSS-animated scene transitions that run as **native OBS transitions**. No video files, no stinger, nothing else to install.
Built by [Love Thy Nerd](https://lovethynerd.com).

* **33 ready-made transitions** (wipes, shapes, themed, minimal, stroked text, tiled logos) appear in OBS as `Forge: <name>`.
* **A built-in designer** (OBS menu **Tools > OBS CSS Stinger Forge...**) lets you change colors, text, logo, speed and save your own.

## Install (Windows)

1. Download **`OBS-CSS-Stinger-Forge-Setup-x.y.z.exe`** from the [latest release](../../releases/latest).
2. Close OBS and run the installer. It puts the plugin in `%ProgramData%\obs-studio\plugins\forge-transitions`.
3. Start OBS. The log (Help > Log Files > View Current Log) shows `OBS CSS Stinger Forge loaded`.

Windows may show a SmartScreen warning because the installer is not code-signed: choose *More info > Run anyway*.
Requires OBS 31 or newer (64-bit) with the built-in Browser source.

## Use

**Ready-made transitions:** in OBS open **Scene Transitions**, press **+**, and choose **Forge: Iris** (or any other `Forge:` entry). Pick it from the dropdown whenever you want it.

**Your own:** choose **Tools > OBS CSS Stinger Forge...** (or the *Open the designer* button in a Forge transition's properties). The designer opens in your browser. Design a transition (colors can be saved as your own palettes), name it, press **Save as OBS transition**, then add it with **+** in Scene Transitions as **Forge: your name**. OBS has no public API for a plugin to add transitions to the dropdown itself, so each one is added once with the + button.

The designer runs from inside OBS, so OBS must be open to use it. Saved and ready-made transitions work without it.

## Notes and limits

* Sync offset (transition properties): the overlay starts a frame or two after the cut logic. Adjust if the scene change feels early or late (default 60 ms).
* Studio Mode T-bar does not drive the animation (it is time-based).
* Transitions are added only through OBS (Scene Transitions > +). There is no browser-source URL or video export.
* Windows only for now, and not yet tested on every OBS release. Please open an issue with your OBS log if something fails.

## Build from source

Push a tag like `v0.2.0` (or run the **Build** workflow in the Actions tab). GitHub Actions builds the plugin against OBS 31.1.1, packages the Setup `.exe`, and attaches both to a release. No Visual Studio needed locally.

Uses [cpp-httplib](https://github.com/yhirose/cpp-httplib) (MIT) for the built-in local server.

## License

GPL-2.0-or-later (see `LICENSE`), matching OBS Studio's plugin template.
