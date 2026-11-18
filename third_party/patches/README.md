# libprojectM patch set

Patches applied, in filename order, on top of the pinned `third_party/projectm` submodule (upstream v4.1.8).
Never commit inside the submodule; change projectM's behaviour only through patches here.
See `technical-details-2026-10-22.md` §3 for what each patch is for.

## How patches are applied

At configure time, `cmake/ProjectMPatched.cmake` copies the submodule to `build/projectm-src`, makes the copy its
own git repository, and runs `git apply` for each patch. The copy is rebuilt whenever the submodule commit or a
patch file changes. A patch that doesn't apply stops the configure step with its name.

## Adding or changing a patch

```sh
scripts/patches.sh start /tmp/pm-work         # worktree at the pinned tag, existing patches applied as commits
# edit files in /tmp/pm-work, then commit (one concern per commit, explain why in the message)
scripts/patches.sh export /tmp/pm-work my-change   # -> the next number, e.g. 0004-my-change.patch
scripts/patches.sh finish /tmp/pm-work
```

To change an existing patch, amend or fix up its commit in the worktree and re-export it over the old file
(`git -C /tmp/pm-work format-patch -1 <commit> --stdout > third_party/patches/<file>`).

## Upgrading projectM

1. Check out the new tag in `third_party/projectm` and commit the submodule bump.
2. `scripts/patches.sh start <dir>`. If `git am` stops, resolve each conflict and re-export the affected patches.
3. Drop patches that upstream now covers (e.g. 0001 once the release has `projectm_opengl_render_frame_fbo`).

## Current patches

| Patch | Purpose |
|---|---|
| `0001-render-to-target-fbo.patch` | Backport of `projectm_opengl_render_frame_fbo()` from upstream 4.2.0 so projectM draws into our offscreen FBO. |
| `0002-layer-texture-api.patch` | `projectm_set_layer_texture`: alpha-blends a caller's finished texture into the frame at a fixed layer (feedback, before the warp; before-composite is stored for a later patch). The app prepares the texture itself (`engine/VideoChain`). |
| `0003-shader-program-cache.patch` | Reuses linked shader programs per thread (macOS's GL driver leaks ~35 KB per program, even after deletion); fixes two GL buffer leaks. |

`0004` and `0005` (feedback contrast, saturation and luma invert) were removed in N2: those effects now run in the app's
own shader before the texture reaches projectM.
