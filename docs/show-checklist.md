# Show checklist

For running a performance with Govtech Visualizer. Written for the 2026-11-06 show; it applies to any show.

## The day before

- [ ] **Build the version you'll perform with**, then don't rebuild on show day:
      `git pull && cmake --build build -j`
- [ ] **Run the whole show once** (open it, trigger every scene from Ableton). This also fills the macOS shader cache, so preparing scenes on the night is quicker.
- [ ] **Check the scene list for warning marks**. An amber `!` means the scene is heavy or dropped frames when it was live (the tooltip says how many ms per frame). Anything over about 16 ms per frame can't run at 60 fps on this Mac. `2h-drop3` (pain candy, about 90-100 ms per frame) plays at about 10 fps: replace it or accept that.
- [ ] **Check every video.** Each scene's `V` badge should go live, and the Scene panel should say "Status: live" when the scene is selected. A red `V` means the file is missing.
- [ ] **Save** (Cmd+S). The window title says "Edited" when there are unsaved changes.
- [ ] Keep a **copy of the performance file** and the video folder on a USB stick.

## Setting up at the venue

- [ ] **Mac settings**
  - Turn on a Focus mode (Do Not Disturb), so notifications can't appear over the visuals.
  - Stop the display and Mac from sleeping: System Settings > Lock Screen (display) and Energy. Or run `caffeinate -d` in Terminal for the duration.
  - Quit other apps that use the GPU (browsers with video, other VJ software).
  - Plug in power.
- [ ] **Displays**: connect the projector and set it to **extend**, not mirror (System Settings > Displays). Its native resolution is used for rendering, capped at 1920 pixels wide.
- [ ] **Audio**: route Ableton into the Mac with Loopback. In the app, open **Setup > Audio input**, pick the Loopback device, and adjust **Gain** until the level bar moves well with the music.
- [ ] **MIDI**: in Audio MIDI Setup, the IAC Driver must be **online**. In Ableton, send CC 20 on **MIDI channel 1** (or set the same channel in **Setup > MIDI**). Send one value: the status bar at the bottom shows `MIDI ch 1 CC 20 = …`, and that scene goes live.
- [ ] **Open the show**. The app reopens the last performance on launch; otherwise use File > Open Recent.
- [ ] Wait until the status bar no longer says "Preparing presets n/m".
- [ ] **Go fullscreen**: drag the window onto the projector, then press **Cmd+F** (or click Fullscreen at the bottom right of the preview). The cursor is hidden in fullscreen. Preset previews pause while fullscreen.
- [ ] Before the doors open: the output stays black until the first scene is triggered. Trigger scene 0 (CC 20 = 0) when you start.

## During the show

- **Cmd+F** or **Esc** leaves fullscreen (the visuals keep running in the preview), and Cmd+F goes back.
- **Left/Right** arrows trigger the previous/next scene if you need to drive it by hand.
- Out-of-order jumps hold the current image for a moment while the scene prepares. Sequential cuts are instant.

## If something goes wrong

| Problem | What to do |
|---|---|
| A scene doesn't change on a MIDI cue | Check the status bar for the CC 20 value it received. A different channel or value means the Ableton clip is sending the wrong one. Unplugged MIDI devices are picked up again automatically when they return. |
| Visuals don't react to the music | **Setup > Audio input**: is the level bar moving? Pick the Loopback device again (Refresh if it was unplugged), and check Loopback is running. |
| A video scene shows no video | The scene's `V` badge or Scene panel status says why: file missing, or (Syphon) "disconnected", meaning the sender isn't running. The preset keeps playing without the video. |
| The projector was unplugged | Plug it back in, leave fullscreen (Cmd+F), drag the window back onto it if macOS moved it, and press Cmd+F again. |
| The app crashed or was quit | Relaunch it. It reopens the last performance; unsaved changes are recovered automatically. Press Cmd+F on the projector and trigger the current scene. |
| Everything is hopelessly stuck | Force quit (Cmd+Option+Esc), relaunch, Cmd+F, trigger. That takes about 10 seconds. |

## Harmless console messages

Running from Terminal shows lines that aren't problems:
- `UNSUPPORTED (log once): POSSIBLE ISSUE: unit 0 GLD_TEXTURE_INDEX_2D is unloadable…`: a preset uses an image texture that isn't in your texture folder. That part renders black; the browser flags such presets.
- HLSL errors like `Undeclared identifier 'sampler_rand00'`: a preset's shader didn't translate; projectM falls back to a simpler version of that preset.
- OpenGL is deprecated on macOS. Apple still ships it, and the app uses it on purpose for now (the plan is to move to Metal later).
