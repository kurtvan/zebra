# ISF-style shaders: technical overview

**Status:** notes for a later sprint, to scope after N1–N6. Nothing here is decided.
**Prompted by:** the PR #42 review question about `VideoChain.cpp`'s shader chaining.

## Why

Today every effect node (Contrast, Saturation, Luma gate, Opacity) is one line of GLSL. `VideoChain` pastes those lines into one combined shader, and a loop with an `if` per pass picks which line runs. That works for these four effects, but it has hard limits:

- **Per-pixel only.** An effect can't read neighbouring pixels or earlier frames, so blur, displacement, edge detection and feedback trails are all impossible.
- **Fixed size.** At most 16 effects per branch, each with two numbers and one flag.
- **Fragile snippets.** The snippets share variable names (`color`, `alpha`, `param`) and are joined as strings, so a mistake shows up as a compile error in the combined shader, not in the node.
- **Built in only.** Every effect has to be compiled into the app; users can't add their own.

We also want shader kinds other than Milkdrop presets eventually. That's why the Preset node became "Milkdrop preset".

## What ISF is

ISF (Interactive Shader Format) is a small, open convention from Vidvox (VDMX) for portable video shaders. It's used by VDMX, Resolume, Millumin, CoGe, Magic and others. Hundreds of free shaders are at isf.video.

An ISF shader is a GLSL fragment shader (`.fs`) that starts with a JSON header inside a comment. It can have an optional vertex shader (`.vs`) next to it.

```glsl
/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Box blur",
    "CATEGORIES": ["Blur"],
    "INPUTS": [
        { "NAME": "inputImage", "TYPE": "image" },
        { "NAME": "radius", "TYPE": "float", "DEFAULT": 4.0, "MIN": 0.0, "MAX": 32.0 }
    ]
}*/

void main()
{
    vec2 step = 1.0 / RENDERSIZE;
    vec4 sum = vec4(0.0);
    for (int i = -4; i <= 4; ++i)
        sum += IMG_NORM_PIXEL(inputImage, isf_FragNormCoord + vec2(float(i) * radius / 4.0, 0.0) * step);
    gl_FragColor = sum / 9.0;
}
```

### The header

- **`INPUTS`** declares what the shader needs. The host turns each input into a uniform and builds a control for it. Input types:
  - `float`: `MIN`, `MAX` and `DEFAULT`
  - `bool`
  - `long`: a choice, with `VALUES` and `LABELS`
  - `color`
  - `point2D`
  - `event`: a one-frame trigger
  - `image`
  - `audio` and `audioFFT`: the host supplies a texture of samples or spectrum
- **`PASSES`** (optional) splits the shader into several render passes.
  - Each pass can render into a named `TARGET` texture that later passes read.
  - `"PERSISTENT": true` keeps a target's texture from frame to frame, which is how feedback and trails work.
  - `"FLOAT": true` asks for a float texture.
  - `WIDTH` and `HEIGHT` can be expressions such as `"$WIDTH/2"`.
- **`IMPORTED`** (optional) names image files bundled with the shader.

**Shader kinds, by convention:**
- **Filter:** has an `image` input called `inputImage`.
- **Transition:** has `startImage`, `endImage` and a `float progress`.
- **Generator:** has neither.

### What the host provides

- **Uniforms:**
  - `TIME`: seconds since start
  - `TIMEDELTA`
  - `FRAMEINDEX`
  - `RENDERSIZE`: the output size in pixels
  - `PASSINDEX`
  - `DATE`
- **Coordinates:** `isf_FragNormCoord` is the pixel position from 0 to 1.
- **Image lookups:**
  - `IMG_NORM_PIXEL(img, uv)` and `IMG_PIXEL(img, pixelCoord)`
  - `IMG_THIS_PIXEL(img)` and `IMG_THIS_NORM_PIXEL(img)`: the current pixel of `img`
  - `IMG_SIZE(img)`
- **GLSL translation:** ISF source is written in old GLSL (`gl_FragColor`, `texture2D`). The host rewrites it for the GL version it runs. We use 3.30 core, so it becomes `out vec4` and `texture()`.

### Reference implementation

**VVISF-GL** (Vidvox, BSD licence, C++) parses ISF files, translates the GLSL, manages the pass textures and renders. We could:

- **(a) Use it:** fastest to a working node, but it brings in its own GL object wrappers.
- **(b) Write our own parser and renderer:** the header is small JSON, which we already read with nlohmann, and the translation is a list of text replacements plus generated uniform declarations. Probably a few hundred lines.

To decide in the sprint. (b) fits our code better; (a) is better tested against the shaders people have actually written.

## How it would fit the node graph

### Rendering: one pass per node

- Each ISF node renders into its own texture, sized to the canvas.
- Its `image` inputs are the textures of whatever is wired into it.
- A chain of N effects is N full-frame passes, ping-ponging between two textures where nothing else reads the intermediate result.
- Multi-pass and persistent shaders own their extra target textures per node instance. They're created when the scene loads and freed when it unloads.
- **Cost:** one full-frame read and write per pass. At 1920×1080 that's cheap for simple filters. Heavy shaders can be slow, so the GPU readout from N6 should show time per node, and we should warn on heavy shaders the way we warn on heavy presets.
- **Later optimisation:** merge neighbouring per-pixel effects into one pass. This is the current combined-shader idea, but as generated code per chain instead of a runtime `if` per pass.

### Model: one node type with a per-file shape

`NodeDef` (PR #42) assumes each type's ports and settings are fixed. An ISF node's shape comes from its file:

- **Node type:** one new `NodeType::IsfShader`, with params `{ path, values by input name }`.
- **Ports:**
  - `image` inputs become Video input ports, named after the input.
  - The output is Video. A generator has no inputs.
  - `inputCount` and `inputKind` already take the node, as the switcher's do, so a per-node shape fits the existing hooks. They would read a cached parsed header for the node's file.
- **Show file:**
  - It stores the path and the values by name.
  - Values for inputs the file no longer has are kept but ignored, as wired textures a preset no longer has are today.
  - A missing file is a warning, as a missing Milkdrop preset is.
- **Validation:** ranges come from each input's `MIN` and `MAX`.

### UI: controls built from the header

Today `NodeUi::controls` is a fixed table per type. An ISF node needs a control list built from its parsed header:

| ISF input | Control |
| --- | --- |
| `float` | Slider (`MIN`, `MAX`) |
| `bool` | Checkbox |
| `long` | Choice (`LABELS`) |
| `color` | Colour picker (new control kind) |
| `point2D` | Two sliders, or a small XY pad (new) |
| `event` | Button (new) |
| `image` | Input row, as on the preset |
| `audio`, `audioFFT` | No control: the app feeds its audio analysis in |

The generic renderer already draws Slider, Checkbox, Choice and input rows. ISF needs a way to make the control list per node instead of per type, plus three new control kinds.

### Our built-in effects

Contrast, Saturation, Luma gate and Opacity could become bundled ISF files, so there's one effect path instead of two. They keep their node types and show-file names, so existing shows are unaffected. The alternative is to keep them as they are and add ISF beside them. Decide in the sprint.

### Inputs from the app

These are a natural fit:
- `TIME` from the app clock.
- The MIDI clock's beats and bars as extra uniforms: an ISF extension, ignored by other hosts.
- Audio and FFT textures from the existing audio input.

Beat-synced shaders are the obvious win over Resolume-style hosts.

## Risks

- **Compile time on scene change:** compile every ISF in a scene when the show loads or the scene is armed, as presets are prepared today, never on the cut.
- **GPU hangs:** a shader with a huge loop can stall the GPU. Mitigations:
  - the heavy-shader warning
  - a time check on first render
  - users test shaders before a show
- **Spec gaps:** some ISF files use GLSL that only works on one vendor's driver. A shader that doesn't compile is an error on its node, with the compiler message in the tooltip.
- **Licences:** shaders on isf.video carry their own licences. We'd only bundle ones whose licences allow it, or ship none and let users add their own.

## Open questions for scoping

1. Use VVISF-GL, or write our own parser and renderer?
2. Should the four built-in effects become ISF files, or stay as they are?
3. Effects only (filters) at first, or generators and transitions too? A generator could replace a video source.
4. How will users pick a shader: a file dialog, or an ISF library panel like the preset browser with favourites and thumbnails?
5. What about multi-pass and persistent buffers: in the first version, or later?
6. Should we add beat and bar uniforms, and what should they be called?
7. Should shaders reload when their file changes, as a shader-writing workflow would want?
