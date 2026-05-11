# Output Viewer

A browser for ComfyUI's output folder. Tree of folders + image files on the left; thumbnail grid for whichever folder is selected on the right. Click any image to open it in the OS's default viewer; double-click (or Enter on) a folder anywhere to navigate into it.

> **Screenshot suggestion:** the full page after a few generations, with the tree expanded to today's date subfolder on the left and the thumb pane showing 8-12 images. Use as the page header image.

## Where the folder comes from

Two settings in [[Settings]] drive this page:

| Setting | Purpose |
| --- | --- |
| `comfyUiOutputFolder` | Path pattern to ComfyUI's `output/` directory. May include a date suffix like `{yyyy-MM-dd}`. |
| `comfyUiTempFolder` | (Not used here.) The temp folder is for the composer's in-progress preview tile, not this page. |

The pattern is interpreted in two parts:

1. **Tree root** - everything up to the first `{`. This is the on-disk folder that anchors the tree. For `C:/ComfyUI/output/{yyyy-MM-dd}` the root is `C:/ComfyUI/output`.
2. **Auto-nav target** - the full pattern with the `{...}` substring fed through `QDateTime::toString`. For the same pattern on 2026-05-11 the target is `C:/ComfyUI/output/2026-05-11`.

When the page loads (or when settings change), the tree is rooted at the root path and - if today's date subfolder already exists - the tree auto-selects it and the thumb pane shows its contents. This is why the page usually drops you straight at "today" without scrolling.

If the root path is empty or doesn't exist, the page hides the tree/thumb split and shows a centered status message telling you to configure the output folder in Settings. The subtitle next to "OUTPUT FOLDERS" reflects the state:

- `(not configured)` - no pattern set in Settings.
- `(missing)` - pattern set but the path doesn't exist.
- The literal root path - everything's wired up.

## Layout

Edge-to-edge horizontal splitter with no outer margins. Both panels have a 50 px header (dark fill + bottom rule, same shape as the [[Workflow Editor]] / [[Prompt History]] headers).

| Column | Header | Body |
| --- | --- | --- |
| Left (320 px default) | `OUTPUT FOLDERS` + the resolved root path as subtitle. | Tree view of the folder hierarchy. |
| Right (flex) | `PREVIEW` + current dir name + image/folder count as subtitle. Plus an open-external icon. | Thumbnail grid (192 x 192 tiles) of the current dir's contents. |

The splitter handle is 5 px wide, can be dragged to resize, can't be collapsed below either panel's minimum.

> **Screenshot suggestion:** close-up of the right pane's header showing the subtitle (`2026-05-11 - 8 images, 2 folders`) and the open-external icon.

## Left column - Output Folders tree

A `QTreeView` backed by a `QFileSystemModel` rooted at the resolved root path. Single column (the file name); size / type / date columns are hidden.

The model filters to image files only (`*.png`, `*.jpg`, `*.jpeg`, `*.webp`, `*.bmp`, `*.gif`) for the file entries; all directories are shown regardless of contents (so you can navigate into folders that hold non-image files). Non-image files in image folders simply don't show up.

### Selection behavior

- **Click on a folder** - selects it; the thumb pane repopulates with that folder's contents.
- **Click on an image** - selects it. The thumb pane shows the image's parent folder's contents and highlights the image's thumb (so you can use the tree as a quick search before browsing visually).
- **Sibling selection within the same parent** doesn't rebuild the thumb pane (the dir hasn't changed); arrow-keying through tree siblings stays snappy.

### Keyboard

| Key | On a folder | On an image |
| --- | --- | --- |
| `Up` / `Down` | Move selection. | Move selection. |
| `Left` / `Right` (default tree behavior) | Collapse / expand the node. | Tree default (no-op for files). |
| `Right` (extra) | - | **Cross to the thumb pane** and focus the matching thumb. |
| `Enter` / `Return` | Toggle expand / collapse. | **Open in the system viewer.** |
| Double-click | Standard tree expand. | **Open in the system viewer.** |

The "Right arrow on a file jumps to the thumb pane" binding is the keyboard-first counterpart to clicking the thumb. Together with `Escape` from the thumb pane (which returns focus to the tree and re-selects the matching tree row), you can ping-pong between the two panes without the mouse.

## Right column - Preview thumb grid

A `QListWidget` in icon mode, 192 x 192 thumbnails with a 24 / 36 px grid padding (so tile spacing breathes). One item per **direct child** of the current directory:

- **Folders first**, alphabetically. Drawn with a generic folder icon at the same 192 x 192 tile size as image thumbs.
- **Image files** next, alphabetically. Drawn with the actual image, decoded and scaled async (see below).

The pane horizontally **centers** its content: the calculated number of tiles per row fits inside the viewport width and the leftover horizontal space gets split evenly between left and right margins via `setViewportMargins`. So the grid never looks left-anchored on a wide screen.

### Subtitle line

Reads `<dir name> - N image(s), M folder(s)`. Updates every time the current dir changes. Empty subtitle for a non-existent / empty dir.

### Open-folder button

A small open-external icon at the right end of the header bar. Clicking it opens the **currently shown** thumb directory in the OS file explorer (via `QDesktopServices::openUrl`). Disabled when the thumb pane has nothing to show.

### Keyboard

| Key | Effect |
| --- | --- |
| `Up` / `Down` / `Left` / `Right` | Move thumb selection (Qt's standard icon-mode navigation). |
| `Enter` / `Return` on a folder | Navigate the tree into that folder (selects + expands). The thumb pane then shows the folder's contents. |
| `Enter` / `Return` on an image | **Open in the system viewer.** |
| `Escape` | Returns focus to the tree, re-selecting the row matching the current thumb selection so they stay in sync. |
| Double-click | Same as Enter (folder navigates, image opens). |
| Single click | Selection only (no activation). |

The "click only selects, double-click / Enter activates" split keeps casual arrow-keying through thumbs from launching the system image viewer on every move.

## Async thumbnail loading

Decoding `output/<huge>.png` at 192x192 with `Qt::SmoothTransformation` is not free. The thumb pane loads them lazily:

1. Every folder change `setEntries` clears the list, bumps a generation counter, and inserts items with a placeholder icon.
2. Each image item dispatches a `QtConcurrent::run` job to load + scale the source PNG on a worker.
3. The worker captures the generation counter and a `QPointer<OutputThumbList>`. When it finishes, it `invokeMethod` back to the GUI thread to swap the placeholder with the real pixmap - but only if the pane still exists AND the generation counter is unchanged.
4. Rapid folder changes (arrow-keying through tree rows) cancel in-flight workers via the generation check, so the pane doesn't fill up with thumbnails from a folder you already left.

In-flight requests are deduped by path so re-entering a folder you just left doesn't kick off a second worker for the same image.

There's no thumbnail cache that survives folder changes - each folder visit re-decodes its contents. The thinking is that you typically scroll through "today's outputs" once and the cache wouldn't earn its memory cost. If this changes, the cache hook is simple to add.

## Coordination with Run flow

This page is **not** what receives the composer's "preview" image after a Run completes - that's the composer page's inline preview tile, sourced from `comfyUiTempFolder` (the temp folder, not the output folder).

This page is the **finished archive** view: ComfyUI eventually writes each completed prompt's final image to `comfyUiOutputFolder/<subfolder>/<filename>.png`. To see new outputs here you need to navigate manually - either expand/select the matching subfolder in the tree, or close-and-reopen the page (which re-evaluates today's date pattern and auto-navigates if today's subfolder exists).

There's no filesystem watcher running, so output files added while you're sitting on the page don't appear until the next folder selection refresh. If you Run, switch to this page, and don't see your new image, click the parent folder and back to force a re-scan.

## Tips

- **Use the date suffix.** Without `{yyyy-MM-dd}`, the page roots at the static output folder and you'll be scrolling through every date's worth of images at once. With it, the page lands on today.
- **Press Enter to view, single-click to scout.** Arrow through thumbs without firing the system viewer every time; press Enter on the one you want to open.
- **Right arrow from the tree to focus a thumb.** When you've found a specific image in the tree (typed-search or scrolled to it), Right jumps to the thumb pane focused on that image so you can scroll context around it visually.
- **Escape from the thumb pane to keep the tree in sync.** Sends focus back to the tree at the selected row - useful when you want to drill further into the file system from where you are.
- **The grid centers itself.** Resizing the window or splitter recomputes the column count and re-centers, so the thumb pane never looks awkward on ultrawide displays.
- **Open the folder externally for bulk work.** The open-external icon next to "PREVIEW" launches the OS file explorer at the current dir - the right move when you want to drag images out, batch-rename, or whatever else the OS does better than this page.
- **Subfolders render as tiles too.** ComfyUI workflows that organize outputs into subfolders (e.g. by experiment name) appear as folder tiles in the thumb pane - click to drill in without touching the tree.

## See also

- [[ComfyUI Integration]] - the run/dispatch flow that produces files in `comfyUiOutputFolder`.
- [[Settings]] - the `comfyUiOutputFolder` field and pattern syntax.
- [[Tag Composer]] - source of generated images (and host of the inline preview tile that's a sibling concept to this page).
- [[Prompt History]] - records the prompt that produced each output (no direct link from filename, but the timestamps line up).
