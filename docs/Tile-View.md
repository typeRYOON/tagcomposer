# Tile View

The main entry browser. A scrolling grid of image tiles on the left, a detail panel on the right, and a single search bar that drives the grid. Most day-to-day work happens here: finding entries, editing their tags, pushing them into the composer.

## Layout

```
+---------------------------------------------+--------------+
|  search tags...                             |   image      |
|  ----------------------------------------   |   (drop)     |
|  +--------+  +--------+  +--------+  ...    |              |
|  | tile A |  | tile B |  | tile C |   nav-->|   title      |
|  +--------+  +--------+  +--------+  panel  |   comment    |
|                                             |              |
|  +--------+  +--------+                     |   tag list   |
|  | tile D |  | tile E |                     |   ...        |
|  +--------+  +--------+                     |              |
+---------------------------------------------+--------------+
   center column (search + grid)               entry panel
```

The grid auto-flows tile width based on available space. The entry panel on the right takes a fixed 480px and shows whichever entry is currently selected.

## Search bar

A single text field across the top of the grid drives `EntryModel::filter`. Typing is debounced 150ms before the query fires.

The full query grammar - tag prefixes, OR groups with `|`, `title:`, `lora:`, `sort:`, `images:`, `tags:`, `has:` / `missing:`, etc. - is documented on the [[Search Queries]] page. Anything that page describes also works here.

### LoRA drop-to-hash

The search bar accepts `.safetensors` files via drag-and-drop. When you drop a LoRA file:

1. The bar disables itself and shows "hashing LoRA...".
2. It computes the SHA-256 off the UI thread.
3. The hash is written into the bar as `lora:<sha256>`, which filters the grid to entries whose stored LoRA matches.

Useful for "which entries use *this* LoRA file" without opening the workflow editor.

## The tile grid

Each tile shows one entry's first image (or a placeholder for entries without an image), with the entry title overlaid at the bottom.

### Visual indicators

A tile can carry several overlays simultaneously - the colors and shapes are chosen so they don't conflict:

| Indicator | Where on the tile | Meaning |
| --- | --- | --- |
| Soft white outer halo | Outside the rounded edge | This entry is currently selected (right-panel target). |
| Green inset border (2.5px) | Just inside the edge | Entry is **pushed to the composer**. Its tags are folded into the active set. |
| Orange ring (2.5px) | Slightly inside the edge | Entry's LoRA is **currently active** in the prompt stack. |
| Small orange square badge | Top-right corner | Entry has a LoRA assigned but it isn't currently active. |
| Scale-down + opacity fade | Whole tile | Mouse hover. Subtle visual feedback only. |
| Fade-in on first paint | Whole tile | Image just decoded from disk; one-shot per session. |

### Selection

- **Click** a tile to select it. Focus jumps to the entry panel's tag search field so you can type new tags right away (useful when adding the same tag across several entries in a row).
- **Arrow keys** (Left/Right/Up/Down) move the selection within the grid. Keyboard navigation deliberately keeps focus on the tile view, so chained arrow presses still go to the grid.
- **Home / End / PgUp / PgDn** also move selection - Home/End go to first/last, PgUp/PgDn move by viewport.
- The selected tile is the one whose data the right-side entry panel reflects. Selection persists across queries (re-filtering will still highlight it when it reappears in the result set).

### Scrolling

- **Mouse wheel** scrolls smoothly. One notch ~= one third of a tile height.
- **Click-and-drag** on empty space (or even on a tile, if you start moving) becomes a drag-scroll. Release with momentum to fling; friction-decays naturally.
- A click that doesn't move past the system drag threshold is treated as a selection click, not a drag.

### Right-click menu

Right-clicking a tile opens a context menu with up to two actions, depending on the entry:

- **Add to Composer** / **Remove from Composer** (only when the entry has at least one image with tags) - toggles the composer push.
- **Activate LoRA** / **Deactivate LoRA** (only when the entry has a LoRA assigned) - toggles inclusion in the active LoRA stack.

## Entry nav panel (floating)

A small handle at the top-right of the tile grid. Hover to expand a vertical list of every entry currently active in the composer (titled, in push order).

Clicking an entry in this list smoothly scrolls the grid to it - useful when you have many entries pushed and want to jump between them without scrolling manually.

The panel only appears when at least one entry is active; otherwise the handle stays minimized.

## Entry panel

The right-hand column. Shows the full state of the selected entry and is the main editing surface.

### Image dropper

The top of the panel shows the entry's current image slot, with prev/next page controls underneath if the entry has multiple images. The image area is a drop target for image files only:

- **Drop an image file** (.png, .jpg, .webp, ...) onto the area to assign it to the current image slot. The file is resized (max 1024 x 1280) and saved into the entry's folder. If the entry has no image slots yet, the drop creates the first one.

LoRA drops are handled separately by the LoRA section (see below), not by the image area.

The page-label (`2 / 3`) under the image tracks the current slot; prev / next buttons step through slots. **Add image** / **Remove image** buttons create / delete slots.

### Title, comment

- The title is a single-line editor at the top of the right column. Edits commit on focus loss or Enter.
- The comment field below is multi-line, plain-text, ~3 lines tall. Notes for yourself; survives all operations and is searchable via the `comment:<term>` query clause.

### Tag list

Each image slot has its own tag list, shown below the search field. Each row is:

```
[colored dot]  tag name         [?]  [x]
```

- **Colored dot**: the Danbooru category color (general, artist, copyright, character, meta).
- **`?` badge**: only visible if the tag has **no facets defined** in `tag_definitions.fct`. Same convention as the composer and dataset helpers - a visual nag for tags you should classify.
- **`x` button**: removes the tag from this image slot.
- The tag name itself is editable in place - press Enter to commit a rename.

Right-clicking a tag opens the same wiki / facet-editor / quick-facet-add menu used elsewhere in the app.

#### Tag search field

A search bar above the tag list with Danbooru-style autocomplete. Typing surfaces matching tags; Enter / Tab / click adds the highlighted suggestion to the current image slot.

**Comma-separated input** is supported: typing or pasting `taga, tagb, tagc` and hitting Enter (or any commit) splits on commas, trims each piece (also stripping surrounding `"`/`'` quotes), and adds them one by one. Already-present tags emit a "tag already present" status hint instead of being added twice. This is the fastest way to bulk-tag an entry from an external source (e.g. a comma-joined tag dump pasted in). The composer coalesces the resulting per-tag signals so a multi-tag paste doesn't trigger one pipeline rebuild per tag - see the [[Tag Composer#coalescing-bursts-of-tag-additions]] section for the mechanics.

When you switch entries (click a tile, or arrow-key navigate), focus auto-parks in this field. This is the primary way to bulk-add the same tag across several entries: select an entry, type tag, Enter, click next entry, type, Enter, ...

### LoRA section

A dedicated "Drop .safetensors / .ckpt" zone in the entry panel binds a LoRA to the current entry:

- **Drag a `.safetensors` / `.ckpt` / `.pt` / `.pth` file** onto the zone, or **left-click** it to open a file picker. Either way, the selected file becomes the entry's LoRA.
- **Right-click** the zone for a context menu (open LoRA folder, show LoRA info from the safetensors metadata, clear, etc.).

Once a LoRA is bound, the zone is replaced by:

- **Filename** (with click-to-clear) - the bound LoRA, relative to one of the configured LoRA roots.
- **Model strength** / **Clip strength** - per-entry weights used when the LoRA is active in the stack. Live-edited spin boxes; changes propagate immediately to any in-progress queue.
- **SHA-256** button - re-computes and updates the cached hash if the file changed on disk.

The two LoRA roots (primary / test) and their default strengths come from Settings.

### Actions

Three buttons live in a row below the image:

- **To Composer** - push the entry into the composer's active set (toggles green border in the grid). Equivalent to the right-click "Add to Composer".
- **Copy tags** - copies the current image slot's tags as a comma-separated string to the clipboard.
- **Delete** - removes the entry entirely. The entry's `data/entry/<uuid>/` folder is **deleted recursively** from disk (images and all), and the entry is dropped from the model and grid. The numeric id is retired permanently (never reclaimed by a future `addEntry`).

## Adding entries

Right-clicking on **empty space** in the tile grid (not on a tile) opens an "Add entry" menu. Selecting it launches a small dialog asking for the new entry's title; the resulting entry has one empty image slot ready for a drop.

Dataset Helpers' tag-cluster page has its own "Create entry" flow that creates an entry pre-seeded with tags from the cluster results.

## Drag-and-drop summary

| Drop target | Accepts | Effect |
| --- | --- | --- |
| Search bar | `.safetensors` | Hashes and filters by `lora:<sha256>`. |
| Image area (entry panel) | image file (.png / .jpg / .webp / ...) | Assigns to the current image slot; resizes and saves. |
| LoRA drop zone (entry panel) | `.safetensors` / `.ckpt` / `.pt` / `.pth` | Binds as the entry's LoRA. |

## Live coordination with other pages

The tile view is the source of truth for "which entries are active". When you push or unpush an entry, the composer's active set updates and the prompt rebuilds. When you change an entry's LoRA strengths, the workflow editor's LoRA stack panel reflects the new values.

When the composer requests an entry by id (e.g. you clicked a related-entry link in a tag row), the tile view receives `selectAndScrollToEntry` and smoothly scrolls to surface the entry, selecting it.

External-page jumps that target an entry (e.g. the [[Prompt History]] page's "click to open in entry viewer" tile action) call `clearSearchAndSelect`, which drops any active query first so the entry is guaranteed to be in the result set.

## Performance notes

- Tile images are baked into pixmaps off the UI thread on a `QThreadPool`. Each entry's first image is decoded and downscaled (with a Danbooru-style gradient + title bar) the first time it scrolls into view, then cached in a bounded LRU.
- The grid pre-loads about three rows ahead of the viewport, so fast scrolls don't wait for decodes.
- Re-queries (typing in the search bar) reuse the cache - tiles already decoded keep their pixmaps.
- Big scroll jumps (more than one viewport) are snapped instead of animated, to avoid thrashing the decode pool on tiles that would flash by in one frame anyway.
