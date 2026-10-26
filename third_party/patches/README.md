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
scripts/patches.sh export /tmp/pm-work feedback-texture-api   # -> 0002-feedback-texture-api.patch
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
| `0004-feedback-contrast-saturation.patch` | `projectm_set_feedback_contrast/_saturation`: colour adjustments of the feedback texture, applied before the luma gate. |
| `0003-shader-program-cache.patch` | Reuses linked shader programs per thread (macOS's GL driver leaks ~35 KB per program, even after deletion); fixes two GL buffer leaks. |
| `0002-feedback-texture-api.patch` | `projectm_set_feedback_texture/_mix/_luma_gate`: blends a caller's texture (video) into the preset's feedback buffer before the warp. |
