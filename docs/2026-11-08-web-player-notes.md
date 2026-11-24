# Web player: early notes (2026-11-08)

Cursory thoughts, not a plan. The idea: play a performance file's visuals in a web page, with no editor.

## Why it looks feasible

- projectM 4.1.8 builds for the web: its CMake has an Emscripten option that switches it to OpenGL ES 3 (WebGL 2) automatically. See `EMSCRIPTEN.md` in the projectM source.
- Our own shader code is small. Only two shaders use desktop GLSL 330: the feedback blend in patch 0002 and `src/engine/VideoOverlay.cpp`. Patches 0004 and 0005 only edit the 0002 shader. Porting is mostly the `#version 300 es` header and precision qualifiers.
- The show logic is portable C++ with nothing macOS-specific: the model and JSON (`src/model`), scene switching, playlists, `CycleTimer` and beat hits (`src/engine`). About 3,400 lines.

## What has to be rebuilt

- **Lanes.** Each lane is a thread with a shared GL context; WebGL has neither. A web version renders every projectM instance on one thread, in one context. Main risk: preset shader compiles land on the render thread and may stutter cuts. Background shader compiling (`KHR_parallel_shader_compile`), where the browser supports it, might help; needs testing.
- **Video.** Pool videos play through an HTML `<video>` element, uploaded to a texture each frame. Syphon has no web equivalent and is dropped.
- **Audio.** Microphone or an audio file through Web Audio, in place of the audio input device.
- **Bundling.** Show files use absolute paths (e.g. `/Users/kurt/Movies/...`). An "Export for web" step would collect the show, its presets, textures and videos into one folder with relative paths, ready to upload.

## Open decisions

1. **What drives cuts on the page?** A fixed timeline, clicks or keys, Web MIDI (Chrome and Edge, not Safari), or a JavaScript API so the host page decides.
2. **Timing without MIDI clock.** Beat-based playlists and beat hits need a tempo: a fixed BPM in the export, beat detection on the audio, or those features off on the web.
3. **Audio source.** Microphone, a supplied track, or both.

## Alternatives considered

- **Stream or record the output:** Syphon into OBS, then an embedded stream or a video file. No code, but nothing reacts to the visitor's audio.
- **[Butterchurn](https://github.com/jberg/butterchurn):** a separate JavaScript/WebGL MilkDrop. Quick for plain presets, but none of our features (video feedback, luma gate, overlay, beat hits, scenes) come with it, and its preset compatibility differs from projectM's.

## Suggested next step

Settle the open decisions in a short requirements doc, then run a spike: projectM plus our feedback patches rendering one exported scene in a browser, with a video and microphone input. That proves the rendering and single-thread switching before anything else is built.
