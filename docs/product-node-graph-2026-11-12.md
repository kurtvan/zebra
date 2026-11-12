# Node Graph: Product Definition

*Drafted 2026-11-12 from `proposal-new-features-nov-11-2-26.md` and the answers to its clarifying questions. Companion document: `technical-node-graph-2026-11-12.md`.*

## Purpose

Replace each scene's fixed preset-and-video settings with a small **node graph** the artist wires by hand. The first version does what the app already does, laid out as nodes, plus four things it can't do today:

- inject video into a preset at **four different layers**, not only the feedback and the overlay;
- feed **one video into several layers**, each with its own look;
- cycle any part of a scene with a **cycler** node, replacing preset and video playlists;
- set the **output canvas size** for the whole show.

The user is still a non-technical visual artist: no text, no code, nothing that can be wired into a state that crashes or shows nothing without saying why.

## Decisions (2026-11-12)

| Question | Decision |
| --- | --- |
| Where a graph lives | One graph per scene. Cutting to a scene swaps the whole graph. |
| How freely nodes wire | Chains with one preset at a time. Several effect nodes in any order; one video can branch into several chains. |
| Effect nodes in the first version | Only today's: luma gate (with invert and softness), contrast, saturation, mix. |
| Injection layers | All four: feedback before warp, before composite, on top, and as a preset texture. |
| Playlists | Replaced by a **cycler** node that contains whole branches and switches between them. |
| Cycler transitions | Cut, as today, with the next branch prepared ahead. |
| Viewport | A per-show output canvas size, shown as the end node of every scene's graph. |
| Where the editor appears | A node canvas for the selected scene replaces the Scene panel's video section; clicking a node shows its controls in the Scene panel. |
| Editing a live scene | Everything applies live, including rewiring, with no stutter. |
| Deadline | None; milestones follow dependencies. |

## Core concepts

| Concept | Definition |
| --- | --- |
| **Graph** | A scene's nodes and the wires between them. Every scene has exactly one. |
| **Node** | One step: a source, an effect, a preset, a cycler or the viewport. Each has inputs on its left, outputs on its right, and controls in the Scene panel. |
| **Wire** | Connects an output to an input of the same kind. A video output can have several wires; an input takes one. |
| **Layer** | Where a preset node takes in video. Each preset node has one input per layer. |
| **Branch** | A chain inside a cycler: what plays for one step of the cycle. |
| **Canvas** | The show's output size, e.g. 1920×1080 or 3840×1080. |

## Nodes

### Video source

- A pool video or a Syphon sender, chosen in the inspector as today.
- Carries today's per-video settings: **Restart on entry** and **Loop** (file videos), plus the pool video's own start point, speed and fit.
- One output, **Video**, which can wire to several chains (fan-out).

### Effect nodes

Each takes **Video** in and gives **Video** out, so they chain in any order.

| Node | Controls | Notes |
| --- | --- | --- |
| **Contrast** | Contrast (0–100, 1 = unchanged) | As today. |
| **Saturation** | Saturation (0–2, 0 = greyscale), Black and white shortcut | As today. |
| **Luma gate** | Threshold, Softness, Invert | Keeps the bright parts (or the dark, inverted); Softness feathers the edge. Today's overlay softness and the feedback gate become one node. |
| **Mix** | Opacity (0–1) | How strongly the video shows in the layer it reaches. |

Order matters and is now the artist's choice. Today the order is fixed: contrast and saturation, then the gate. Converted shows keep that order.

### Preset

- One preset, chosen by dragging from the Presets tab onto the node, or onto the canvas to create one.
- One output, **Frame**, the finished picture.
- Four video inputs, one per layer. Any can be left empty.

| Layer | What the video does |
| --- | --- |
| **Feedback** | Mixed into the previous frame before the preset's warp, so its motion smears and distorts the video. Today's video blend. |
| **Before composite** | Added after the preset's warp, waves and shapes, so the composite shader colours and distorts it, but it doesn't smear over time. |
| **On top** | Drawn crisp over the finished frame. Today's Overlay on top. |
| **Texture** | Replaces one of the preset's texture images, so its shaders sample the video. The inspector lists the textures the preset uses; it only affects presets that use textures. |

### Cycler

- Contains several **branches** and shows one at a time, switching every N seconds, beats or bars, in order or shuffled. The interval rules are today's playlist rules: beats and bars follow the MIDI clock and change only on bar lines.
- A branch is a chain of any nodes except another cycler. Each step can therefore carry its own preset and its own look.
- It has the same kind of output as its branches: **Frame** when the branches end in a preset, **Video** when they end in a video.
- Today's preset playlist with per-preset looks, the video playlist, and "same preset, different looks" are all cyclers.
- Cuts are clean: the next branch's preset and video are prepared before the switch. If one isn't ready at the bar line, it waits for the next bar, as today.
- Double-clicking a branch goes live on it, and the cycle carries on from there.

### Viewport

- The end node of every scene's graph; it can't be deleted. It takes the **Frame** to show.
- Its controls set the **show's canvas size**: **Match screen** (today's behaviour, and the default), a preset list (1920×1080, 1280×720, 3840×2160, 3840×1080) or a custom width and height. Changing it in one scene changes it for the whole show, and the node says so.
- The canvas is scaled to fit the output screen, with black bars when the shapes differ.

## The editor

- The Scene panel's video section is replaced by the selected scene's **node canvas**, with pan and zoom. The scene's name, CC value and beat hits stay in the Scene panel.
- **Clicking a node** shows its controls in the Scene panel, the same sliders as today.
- **Adding nodes:** right-click the canvas, or drag a preset or video onto it.
- **Wiring:** drag from an output to an input. Inputs and outputs that can't connect are dimmed while dragging; a second wire into a taken input replaces the first.
- **Validation:** a graph that would show nothing (no preset reaching the viewport, or two presets active at once) is outlined in amber with a one-line reason. The scene keeps showing its last working graph until fixed.
- **Everything is live:** changing values, adding, removing and rewiring nodes on the live scene apply on the next frame without a stutter. Swapping the preset on a live preset node behaves like a cut to it: it plays as soon as it's prepared.
- **Undo:** every edit is one undo step, as today.

## Existing shows

- Shows saved before this change open automatically as graphs that look and behave exactly as before. Saving writes the new format, and the app keeps reading the old one.
- The conversion:

| Today | Becomes |
| --- | --- |
| Preset | Preset node into the viewport |
| Video with its look | Video source, then Contrast, Saturation, Luma gate and Mix, into the preset's Feedback layer |
| Invert luma gate | Invert on the Luma gate node |
| Overlay on top, Softness | A second branch from the same video: Contrast, Saturation, Luma gate (with that softness), Mix, into the On top layer |
| Preset playlist with per-preset looks | A cycler with one branch per preset, each with its own look |
| Video playlist | A cycler of video sources |

## Out of scope for the first version

- New effect nodes (transform, colour, chroma key). Candidates for later, along with the ideas in the TouchDesigner comparison doc.
- Several presets rendering at once, blend nodes between presets, and crossfades.
- Cyclers inside cyclers.
- One graph for the whole performance, or a shared output graph.
- Per-scene canvas sizes, cropping and multi-display output.
- Beat hits as a node; they stay a scene setting.

## Settled after the first draft (2026-11-12)

1. **Canvas above 1920 wide:** allowed. Today's 1920 cap only applies to Match screen; a chosen canvas such as 3840×1080 or 4K renders at that size, with a warning when frames run late.
2. **Texture layer:** when a preset uses several textures, the inspector shows each one's name and a thumbnail from the texture folder.
3. **Cycler branches:** shown inline on the canvas, inside the cycler node.
