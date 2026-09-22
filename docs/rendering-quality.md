# Live2D rendering quality and resource ownership

The fidelity reference is the author's Cubism output using the same exported
assets, parameters and display conditions. The implementation follows the
installed Cubism 5 r.5 renderer's blending, clipping and multiply/screen color
math. Comparing against another desktop pet application is useful for finding
regressions, but does not establish agreement with the editor.

## Texture contract

Model atlases retain their source dimensions and texels on every platform.
Window size and atlas size do not establish the pixel density of individual
parts. A 4096 atlas can contain a small drawing in one corner; reducing the
whole atlas to 2048 halves the resolution of that drawing too. This caused the
visible detail loss in Candya while smaller bundled atlases were unaffected.

The loader does not resample model atlases or silently reduce their dimensions
after allocation failure. Unsupported hardware sizes produce a load error
before pixel decoding where image metadata is available. Thumbnail resizing
is independent of model texture loading.

Textures use uncompressed RGBA8, with alpha premultiplied exactly once before
filtering. The GPU builds the mip chain once during loading. Magnification uses
linear filtering; minification uses trilinear filtering with up to 8x anisotropy
where supported. If mip allocation fails, the entire attempted texture is
deleted before retrying the original pixels with linear filtering. A failed
base-level allocation is reported as an error. This fallback preserves source
detail but has less stable minification; it is logged explicitly.

Model samplers use Cubism's repeat addressing for both color and mask draws.
The generated SDK sampler patch leaves shader arithmetic unchanged and removes
redundant per-draw sampler writes, including writes that would invalidate the
linear fallback. Uploads preserve the caller's texture binding, pixel-unpack
buffer and unpack layout. Ordinary image/UI textures keep straight alpha and
clamp-to-edge addressing.

## Masks, projection and frame state

Drawable and offscreen masks have separate rectangular dimensions. Capacity
includes hidden expressions, respects Cubism's four-channel packing and counts
shared mask combinations once. Dimensions account for subdivisions and the
SDK's 5% margin on each side. Allocations grow immediately and shrink with
hysteresis to avoid repeated reallocations during resizing.

This is a viewport-based density budget, not an arbitrary 4096-pixel or 32 MiB
cap. The GPU size limit still applies and is logged when it restricts precision.
It is not a proof of editor-identical masks for arbitrary geometry extending
beyond the viewport. New GL targets must be complete before replacing the old
ones; a failed resize keeps existing masks and avoids retrying every frame at
the same size. Initial mask allocation failure rejects the model load.

Projection uses a copy of the authored model matrix, so portrait rendering
cannot modify subsequent landscape rendering. Overall model opacity is passed
through Cubism's model color interface. During drawing, framebuffer sRGB
conversion is disabled for the SDK's RGBA8 color math and restored afterward.
This does not implement an editor/display ICC or HDR color-management system.

## Code map

| Module | Responsibility |
| --- | --- |
| `src/media/image_model.c` | Model decode, source-size validation, load progress, alpha occupancy mask |
| `src/media/image_upload.c` | GL upload state, premultiplication, sampler configuration, upload failure recovery |
| `src/media/image_mipmap.c` | GPU mip generation for premultiplied pixels |
| `src/media/image.c`, `image_resize.c` | Ordinary images, thumbnails and image compositing |
| `src/live2d/cubism_model_textures.cpp` | Model texture ownership and renderer binding |
| `src/live2d/cubism_model_masks.cpp`, `cubism_mask_policy.hpp` | Mask capacity, dimensions and replacement allocation |
| `src/live2d/cubism_target_bindings.hpp` | Scope-based restoration of allocation bindings |
| `src/live2d/cubism_model_projection.cpp` | Resize and projection without changing authored layout |
| `src/live2d/cubism_model_viewport.cpp` | Content viewport and lazy visible-state queries |
| `src/live2d/cubism_model_renderer.cpp` | Renderer lifetime, frame state, opacity and offscreen pool trimming |

## Resource costs

A 4096x4096 RGBA8 base texture occupies 64 MiB; its full mip chain brings that
to about 85.3 MiB, excluding driver overhead. The former 2048 cap used about
21.3 MiB by discarding detail. Restoring that detail requires additional texture
storage. GPU mip generation removes application-side CPU mip buffers, which
previously peaked at another 20 MiB for a 4096 source, and their filtering work.
The decoded 64 MiB pixel buffer is temporary and freed after upload. Decoder and
driver allocations can add to the loading peak. Only the existing small alpha
mask stays on the CPU for model bounds. Atlases are loaded one at a time.

Rectangular masks avoid unused space in square targets on wide or tall windows.
Visible triangle bounds are computed only when pointer anchoring or diagnostics
request them, then cached until geometry or projection changes. The first
completed frame of a replacement renderer trims unused SDK offscreen pool
targets. Active targets and the existing deferred model retirement are retained.
The runtime's existing unchanged-frame and hidden-window scheduling remains
responsible for avoiding unnecessary rendering.

## Regression and visual acceptance

`image-filter` covers original dimensions and every base texel on both decoder
paths, large/odd/narrow atlases, alpha-safe mips, GPU-size rejection, and unpack
state isolation. `image-upload-fallback` injects failure after mip storage
exists, checking original pixels, single premultiplication and removal of mips.
`live2d-mask-policy` covers layout boundaries, rectangular density, resize
hysteresis and overflow. `live2d-render-resources` checks allocation under a
bound unpack PBO, distinct read/draw framebuffer restoration, exception cleanup,
replacement of bound targets, and unused offscreen pool reclamation.
`cubism-texture-sampling` checks the SDK patch without compiling: texture
bindings remain and everything outside sampler setup remains byte-identical.

After building, run:

```sh
ctest --test-dir <build-dir> -C Release -R "image-(filter|upload-fallback)|live2d-(mask-policy|render-resources|core-profile)|cubism-texture-sampling" --output-on-failure
```

For visual acceptance, compare Candya and the default model with Cubism using
the same export, parameters, expressions, animation time, scale, background
and display DPI/color conditions. Inspect hair, eyes, fine outlines, translucent
edges, inverted masks, multiply/screen colors and model-opacity fades at small,
normal and enlarged sizes. Include portrait-to-landscape resizing, mirrored
rendering, modern offscreen blend modes and model switching. Measure steady
state CPU/GPU use and loading peaks separately. Texture-level and source checks
alone do not establish pixel-for-pixel editor agreement or application-level
performance gains.
