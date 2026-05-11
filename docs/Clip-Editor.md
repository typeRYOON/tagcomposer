# Clip Editor

The modal mask / crop editor for an Image-typed workflow variable. Opened from the [[Workflow Editor]]'s **Edit...** button on an Image variable card; produces an `ImageEdits` payload (crop rect + optional mask + trim flag) that the variable adopts on Apply.

The output drives what gets uploaded to ComfyUI for that variable at run time: either a source-sized image with a painted alpha mask, or a cropped sub-rect of the source. See [[Workflow Variables#image-input-cache-reference]] for how the result lands in ComfyUI's `input/tagcomposer/` folder.

> **Screenshot suggestion:** the dialog with the source image loaded, a green-tinted mask painted across the subject, the sidebar visible with the Brush tool selected, brush size at ~50, and the rect-bbox status line at the bottom. Use as the page header image.

## Layout

A `ChromedDialog` (frameless modal with the app's title bar). Minimum 920 x 920, default 1024 x 920.

Two columns:

- **Sidebar (220 px fixed, left)** - tool palette, modifier, undo/redo, sliders, overlay tint controls, mask actions, trim-mode toggle.
- **Canvas (flex, right)** - the source image with the live mask overlay, zoom/pan, drag-to-paint.

A status row at the bottom of the dialog spans the full width: the live bbox read-out on the left, the Apply / Cancel buttons on the right.

## The mask model

The dialog edits a single **grayscale, source-sized mask image**:

- Stored internally as `QImage::Format_Grayscale8`, same dimensions as the source.
- `0` = mask off (clear); `255` = mask on. (Anti-aliasing on brush edges can produce intermediate values.)
- Every tool writes into this single mask. There's no layer system - draws are commutative within a session, conflicts resolved by undo.

The mask is **always** the same size as the source image. Zoom and pan are a display-only convenience; you're painting source pixels regardless of how zoomed in the canvas is.

The overlay you see during editing is a **red/green tinted preview** of the mask drawn on top of the source - see [Overlay tint](#overlay-tint) below. The persisted output is the raw grayscale mask, not the tinted overlay.

## Tools (sidebar)

Four exclusive tools in a 2x2 grid at the top of the sidebar:

> **Screenshot suggestion:** close-up of the sidebar's tool palette + Erase + Undo / Redo + brush slider with annotations on which slider applies to which tool.

### Rect

Click-drag to define a rectangular region. Releasing the mouse commits a filled rectangle of `255` (or `0` if **Erase** is on) into the mask. Mid-drag a preview outline shows the pending rect; canceling the drag (releasing outside the canvas, switching tools) discards it.

Use Rect when the region you want is a clean axis-aligned box - portraits cropped to a square, banner-strip cuts, ROI for ControlNet input.

### Brush (default)

Click-drag to paint into the mask with a circular brush. The brush size (in source pixels) is controlled by the **Size** slider in the sidebar. Anti-aliased at the edges, so thin features blend rather than alias.

With **Erase** on, the brush subtracts from the mask instead of adding to it.

### Bucket

Click to flood-fill a region of **similar source pixels** into the mask. The **Tolerance** slider controls how far the flood walks (Hamming distance in pixel values; 0 = exact-color flood, 64 = very permissive). Tolerance is shown next to the slider.

Use Bucket when the region you want has a roughly uniform color or value (a sky, a solid background, a flat character cell). With **Erase** on, the bucket clears mask pixels of the targeted color band instead.

### Mask Fill

Click to flood-fill the **empty mask region** at the click point until it hits a painted-mask boundary (or the image edge). Doesn't look at the source image at all - it walks the mask itself.

The use case is "I painted the outline of the shape I want, now fill the inside." Paint the perimeter with Brush, click the inside with Mask Fill, done.

With **Erase** on, Mask Fill works the other way: click inside a contained mask region to clear it (the flood walks the *painted* mask area until it hits the image edge or an empty region).

### Erase modifier

A toggle button below the tool grid. **Not a tool** - it's a modifier that flips Add/Remove on whichever of the four tools is active. Stays on across tool switches until you toggle it off.

So Brush + Erase off = paint; Brush + Erase on = wipe. Rect + Erase off = fill rect; Rect + Erase on = clear rect.

### Undo / Redo

Below the Erase button. Each tool action (mouse press -> release, even within a single drag) is one history entry. Up to **32** entries; older actions fall off as new ones come in.

Keyboard: **Ctrl+Z** for undo, **Ctrl+Shift+Z** / **Ctrl+Y** for redo (both `QKeySequence::Undo` / `QKeySequence::Redo`, so platform conventions apply).

## Sliders (sidebar)

### Size

Brush radius in source pixels. Range 1-200, default 30. The label updates live (`Size NN`) so you can see what you're committing to as you drag.

Only applies to Brush. Disabled when other tools are active.

### Tolerance

Bucket flood threshold. Range 0-255 (configured), default 16. Higher = more permissive. The label updates live (`Tolerance NN`).

Only applies to Bucket. Disabled when other tools are active.

## Overlay tint

> **Screenshot suggestion:** close-up of the sidebar's color swatch + opacity slider, with a paragraph of the source image visible to show how the tint reads against different backgrounds.

| Control | Effect |
| --- | --- |
| **Color swatch** (24x24 button) | Click to open a color picker (wrapped in a ChromedDialog). The chosen color tints the in-editor overlay. Defaults to `#66aa66` (green). Source image is unchanged. |
| **Opacity** slider | 0-255 alpha on the overlay. Default 110 (`~43%`). The label updates live (`Opacity NN`). |

The tint is purely cosmetic - it doesn't affect the saved mask. Pick a color that contrasts with the source: green on a blue/red scene, magenta on a green scene, etc.

## Mask actions

Three buttons + one checkbox at the bottom of the sidebar:

| Control | Effect |
| --- | --- |
| **Clear mask** | Resets every mask pixel to 0. Push onto undo first (so a misclick is recoverable). |
| **Invert mask** | Flips every mask pixel (`v -> 255-v`). Useful when it's easier to paint the keep region than the mask region. |
| **Crop only (no mask)** | The trim-mode toggle. See below. |

### Crop only (no mask) - trim mode

When **on**:

- The dialog auto-clears the mask on toggle (since trim mode renders the mask irrelevant).
- The brush, bucket, mask-fill tools and the erase modifier are **disabled** - only Rect remains usable, since trim mode is "just a crop".
- The status line below the canvas reads `Crop bbox: ...` instead of `Mask bbox: ...`.
- On Apply, no mask is saved (`maskId` stays empty). The variable card label shows `cropped WxH` instead of `mask WxH`.

When **off** (default):

- Full tool palette is available.
- The mask's bounding box is used as the `cropRect` and the painted mask itself is saved separately for ComfyUI's `LoadImage` to consume.

Use trim mode when the workflow only needs a sub-rect of the source (composition crops for upscaling, etc.). Leave it off for inpainting / region-aware workflows that need an alpha channel.

## Canvas

The right pane shows the source image with the live overlay drawn on top. The canvas widget is `mouseTracking`-enabled so the cursor preview (brush outline, rect preview) follows the mouse without requiring a click.

### Zoom

| Input | Effect |
| --- | --- |
| Mouse wheel up | Zoom in (anchored to the cursor position). |
| Mouse wheel down | Zoom out. |

Range 0.5x to 64x. Cursor-anchored math is sub-pixel accurate so dozens of wheel ticks in one direction don't drift the image off-center.

### Pan

| Input | Effect |
| --- | --- |
| Middle-click drag | Pan the view. |
| Drag from outside the image into / inside it | Pan when you start the drag outside the image bounds (i.e. the margin around the fitted display). |

### Drawing

Left-click and drag with the active tool. Right-click is reserved for future use; for now it doesn't paint.

## Bottom status row

Two parts:

- **Bbox label** (left, word-wraps) - live read-out of the mask's bounding box. Format:
  - Empty mask: `No mask painted - output will be the source unchanged.`
  - With a mask, trim off: `Mask bbox: x=N y=N  W × H`
  - With a mask, trim on: `Crop bbox: x=N y=N  W × H`
- **Apply / Cancel** buttons (right) - standard `QDialogButtonBox::Ok | Cancel`. "Apply" is the "Ok" button's label.

The Apply button is always enabled. Clicking it with an empty mask is the equivalent of canceling (the result is `ImageEdits{}` with `enabled = false`).

## On Apply - the result `ImageEdits` payload

The dialog returns a `core::ImageEdits` value via `result()`. The Workflow Editor's caller adopts it into the variable's `imageEdits` field. The translation:

| Final mask state | Trim mode | Result |
| --- | --- | --- |
| Empty | (either) | `ImageEdits{}` (disabled). Caller drops any prior maskId and renders the variable in its "no edits" state. |
| Non-empty | Off | `enabled = true; cropRect = mask bbox; trimToCrop = false; maskId = saveMask(finalMask)`. ComfyUI's LoadImage sees the source image as IMAGE and the painted mask as MASK. |
| Non-empty | On | `enabled = true; cropRect = mask bbox; trimToCrop = true; maskId = ""`. ComfyUI's LoadImage sees the *cropped* source as IMAGE; MASK is fully transparent. |

A non-empty mask in trim-mode is interpreted only for its bounding box - the actual mask values are discarded on save (the `maskId` stays empty). Useful for "pick a region without committing to an alpha shape".

The previous `maskId`, if any, is **not** removed by this dialog. The caller (`WorkflowEditPage::editBtn` lambda) handles cleanup: it compares the old vs new `maskId` and removes the stale file from the input cache.

## Keyboard

| Key | Effect |
| --- | --- |
| **Ctrl+Z** | Undo. |
| **Ctrl+Shift+Z** / **Ctrl+Y** | Redo. |
| **Esc** | Reject (Cancel the dialog). |
| **Enter** on Apply | Accept. |

The four tools don't have keyboard shortcuts - the sidebar buttons are the only switching surface.

## Tips

- **Paint the perimeter, click inside with Mask Fill.** Faster than brushing the interior of a large region. Same trick for clearing a contained area: paint the outline, Erase + Mask Fill into the inside.
- **Tolerance scales fast.** A bucket tolerance of 32 is already pretty wide on a typical illustration; 64 is "engulf everything visually adjacent". Start low and bump up if the flood underselects.
- **Pick an overlay color that contrasts with your image.** Default green is great on greyscale and red-heavy scenes; for green-heavy scenes (foliage, etc.) try magenta or cyan via the color swatch.
- **Use Invert when the keep region is smaller.** Painting a face is easier than painting the room around it. Mask the face, Invert, and you've selected the room.
- **Trim mode is a permanent mode swap for the saved variant.** Toggling it off and back on does not restore the previously-saved mask - it cleared on toggle. If you want to flip the variant between trim and mask later, undo back to before the toggle (32-deep history permitting).
- **Zoom anchors at the cursor.** Wheel-zoom toward the part of the image you want to inspect; pan otherwise to recenter.
- **Saving is per-edit.** Every Apply call writes a fresh `maskId` (UUID), so the previous mask file becomes orphaned. The Workflow Editor's caller cleans it up automatically; the [[Settings#input-images]] "Clear unused inputs" action is the catch-all for orphans left by other paths.

## Lifecycle and persistence

- Opened only from the [[Workflow Editor]]'s Image variable card -> **Edit...** button.
- On accept, writes a fresh mask file under `data/workflow_inputs/_masks/<uuid>.png` via `WorkflowInputCache::saveMask`.
- On reject (Cancel / Esc), no disk writes. The variable card keeps its previous `ImageEdits`.
- The variable's `imageEdits.hash()` is what the [[ComfyUI Integration]] upload tracker keys on, so an edit change forces a re-upload at the next Run regardless of whether the source image's uuid changed.

## See also

- [[Workflow Editor]] - the page that hosts the Image variable card and the Edit... button.
- [[Workflow Variables]] - the Image-type substitution behavior + the input cache layout.
- [[ComfyUI Integration]] - how the edited image gets uploaded before each Run.
- [[Settings]] - "Clear unused inputs" sweep that removes orphaned masks.
