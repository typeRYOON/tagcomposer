# Keyboard Shortcuts

The full catalog of key bindings, organized by where they fire. Most are app-wide; the rest are page-local. Bindings work regardless of focus *unless* otherwise noted - notably, the **app-wide Run / Interrupt shortcuts skip when focus is in a text input** so typing tags doesn't accidentally queue a prompt.

## App-wide

Bindings live on the main window with `Qt::WindowShortcut` scope, so they fire from any page. The preview-popout window re-binds the same set so they work there too.

| Shortcut | Effect | Notes |
| --- | --- | --- |
| **Shift+E** | Trigger Run on the [[Tag Composer]] using its current count. | Skipped when focus is on a `QLineEdit` or `QPlainTextEdit`. |
| **Shift+R** | Interrupt the in-flight ComfyUI job. | Skipped when focus is on a text input. |
| **Shift+Alt+R** | Clear pending queue (drops queued prompts; the in-flight job finishes naturally). | Skipped when focus is on a text input. |
| **F11** | Toggle fullscreen with a 200 ms fade. | Cross-fades the window opacity around the state swap so the flicker is hidden. |
| **Esc** | Exit fullscreen. | Only when fullscreen; otherwise the key falls through to focused-widget handling. |
| **Ctrl+H** | Minimize with a 200 ms fade-out. | No-op if already minimized. |
| **Ctrl+W** | Close the window (exits the app). | |
| **Ctrl+PgUp** | Previous page in the nav order (wraps). | Same order as the left-edge nav bar: Home -> Tile View -> Tag Composer -> Workflow Editor -> Facet Editor -> Prompt History -> Output Viewer -> Dataset Helpers -> Danbooru Wiki -> Settings. |
| **Ctrl+PgDn** | Next page in the nav order (wraps). | |

### Text-input guard

The Run / Interrupt / Clear-pending shortcuts check whether the currently focused widget is a `QLineEdit` or `QPlainTextEdit` and bail if it is. This is what lets you type freely in tag fields, comment boxes, and search bars without `e` or `r` triggering a queue action. If the shortcut feels unresponsive, check whether your focus is in a text field - clicking out of it (or pressing `Esc` to drop focus back to the page) restores the binding.

## Tile View

See [[Tile View#selection]] for the full discussion. Quick reference:

| Shortcut | Effect |
| --- | --- |
| **Left / Right / Up / Down** | Move tile selection within the grid. |
| **Home / End** | First / last entry in the current filter result set. |
| **PgUp / PgDn** | Selection moves by approximately one viewport. |

Mouse click selects the tile and parks focus in the entry panel's tag search field; arrow-key navigation deliberately keeps focus on the grid so chained arrow presses stay snappy.

## Tag Composer

The composer's full keyboard reference is on [[Tag Composer#keyboard-shortcuts]]. Highlights:

| Shortcut | Effect |
| --- | --- |
| **Ctrl+Z** | Undo. |
| **Ctrl+Shift+Z** | Redo. |
| **Down** in the search bar (empty) | Focus the first tag row in the groups list. |
| **Esc** in the search bar | Return focus to the groups list (or the page, when the empty-state hint is up). |
| **Tab** in the groups scroll | Jump to the search bar. |
| **Up / Down** in the groups list | Move row selection. |
| **Delete** on a selected row | Remove the row's tag. |
| Printable key in the groups list | Focuses the row's inline editor and replays the keystroke. |

The composer's groups scroll also accepts the app-wide **Shift+E / Shift+R / Shift+Alt+R** bindings even when focus is inside the scrolled area - the `ComposerScrollArea` widget re-handles them in its `keyPressEvent` so they fire regardless of which child widget the user clicked into first.

## Facet Editor

The facet editor is the most keyboard-driven page. Full bindings on [[Facet Editor#keyboard-shortcuts]]. Highlights:

| Shortcut | Where | Effect |
| --- | --- | --- |
| Type any printable key | On a list row | Forwards to the filter and focuses it - "just start typing". |
| **Enter** in the filter | Filter | Loads the typed string as the selected tag (existing match or brand-new). |
| **Right** | On a list row | Jumps to the first visible pill in the middle panel. |
| **Left / Right / Up / Down** | On a pill | Spatial neighbor navigation (scrolls into view). |
| **Space** / **Enter** | On a pill | Toggles the pill. |
| **Shift+Enter** | On a pill or facet filter | Clicks the Save button without leaving the checklist. |
| **Esc** | On a pill | Returns focus to the source list (undefined-in-composer if visible). |

The page's "fast triage loop" (no mouse) walkthrough on [[Facet Editor#fast-triage-loop-no-mouse]] chains these into a 5-step pattern for clearing the undefined-in-composer list.

## Output Viewer

See [[Output Viewer#keyboard]] for the full table. Highlights:

### Tree

| Shortcut | Effect |
| --- | --- |
| **Up / Down** | Move selection. |
| **Left / Right** (folders) | Collapse / expand. |
| **Right** (image) | Cross to the thumb pane and focus the matching thumb. |
| **Enter / Return** (folder) | Toggle expand / collapse. |
| **Enter / Return** (image) | Open in system image viewer. |

### Thumb grid

| Shortcut | Effect |
| --- | --- |
| **Up / Down / Left / Right** | Move thumb selection (Qt icon-mode standard). |
| **Enter / Return** (folder thumb) | Navigate the tree into the folder. |
| **Enter / Return** (image thumb) | Open in system image viewer. |
| **Esc** | Return focus to the tree, re-selecting the matching row. |

The Right-arrow-from-tree-jumps-to-thumb + Esc-from-thumb-returns-to-tree pair lets you ping-pong between the two panes without the mouse.

## Danbooru Wiki

See [[Danbooru Wiki#keyboard]]. Highlights:

| Shortcut | Effect |
| --- | --- |
| **Alt+Left** | Back in the in-page history. |
| **Alt+Right** | Forward. |
| **Enter** in the search bar | Look up the typed tag (after autocomplete commits it). |
| **Down** in an empty search bar | Focuses the first autocomplete suggestion (`TagSearchBar` default). |
| Wheel | Smooth-scrolls the body (the viewport's wheel event is filtered to animate). |

The body's `QTextBrowser` is set to `Qt::NoFocus`, so the back/forward shortcuts always fire regardless of where you've clicked on the page.

## Dataset Helpers

The helper tabs each have their own bindings. Most reuse the `TagSearchBar` standard set; the [[Dataset Helpers#tag-editor]] tab has its own page-flip shortcuts on the left panel:

| Shortcut | Where | Effect |
| --- | --- | --- |
| **Left** | Tag Editor left panel | Previous image (-1). |
| **Right** | Tag Editor left panel | Next image (+1). |

These re-use Qt's default focus traversal for arrow keys when focus is on the navigation buttons; the shortcuts themselves are scoped to the left panel widget so typing in the right-side tag editor doesn't trigger them.

The cluster page, auto-collect, auto-tagger, and batch-edit tabs are mouse-driven for their controls (run, browse, etc.). All text fields support the standard Qt editing keys (`Ctrl+A` select all, `Ctrl+C` copy, etc.).

## Tag input bars (the `TagSearchBar` widget)

The composer, the tile-view entry panel, and the wiki page's top bar all share the same `TagSearchBar` widget. Its bindings are consistent across all three:

| Shortcut | Effect |
| --- | --- |
| Type | Surfaces autocomplete suggestions (debounced). |
| **Enter** / **Tab** / click on a suggestion | Commits the highlighted suggestion as the tag. |
| **Down** in an empty input | Emits `downArrowOnEmpty` - the host typically focuses the list below the bar. |
| **Esc** | Closes the popup and emits `escapePressed` - the host typically returns focus to its content. |
| **Enter** with comma-separated text | Splits on commas and adds each piece (strips surrounding quotes). See [[Tile View#tag-search-field]] / [[Tag Composer#search-bar-tag-input]]. |

## Preview popout window

The composer's preview tile pops out into a separate top-level window when clicked. The popout re-binds the app-wide shortcuts under `Qt::WindowShortcut` scope so they work when the popout has focus:

| Shortcut | Effect |
| --- | --- |
| **F11** | Toggle popout fullscreen. |
| **Shift+E** | Run (forwarded to the main window via signal). |
| **Shift+R** | Interrupt. |
| **Shift+Alt+R** | Clear pending. |
| **Ctrl+W** | Close the popout (returns the inline preview tile to the composer). |

Same text-input guard as the main window - the shortcuts skip if focus is on a text input inside the popout (rare, since the popout is mostly a viewer).

## Clip editor dialog

The image edit dialog (crop / mask) launched from a [[Workflow Editor]] Image variable's **Edit...** button uses standard Qt undo / redo:

| Shortcut | Effect |
| --- | --- |
| **Ctrl+Z** (`QKeySequence::Undo`) | Undo the last canvas action (crop adjust, mask stroke, ...). |
| **Ctrl+Shift+Z** / **Ctrl+Y** (`QKeySequence::Redo`) | Redo. |

The dialog's per-tool key bindings and the tool/erase/brush-size controls are documented on [[Clip Editor]].

## Lists in general

Every list widget in the app uses Qt's standard list navigation:

| Shortcut | Effect |
| --- | --- |
| **Up / Down** | Move row selection. |
| **Home / End** | First / last row. |
| **PgUp / PgDn** | Viewport-sized jumps. |
| Type first character | Jumps to the next item whose text starts with that character (where the list isn't already consuming the key for filter input). |

## Tips

- **Esc is your "drop text focus" button.** When `Shift+E` won't fire, you're probably focused in a text field. Pressing `Esc` (in most contexts) gets you out, then the app-wide shortcut works.
- **Use Ctrl+PgUp / PgDn for fast page switching.** Faster than clicking the nav bar - especially when iterating between the composer and the tile view.
- **Preview popout supports the same shortcuts.** If the popout is the foreground window, the keys still trigger Run / Interrupt / Clear-pending without you having to alt-tab back.
- **Composer keystrokes work even from outside the groups list.** `ComposerScrollArea` re-handles `Shift+E / Shift+R / Shift+Alt+R` so clicking into a tag row and pressing Shift+E still queues. The text-input guard means you can't trigger Run by mistake while inline-renaming a tag.
- **Tile view arrow keys ignore the tag panel.** Selecting a tile parks focus in the entry-panel tag search field, but arrow keys still move tile selection because they're handled at the tile-view level. The two are isolated by design.

## See also

- [[Tile View]] - arrow-key navigation, tile selection mechanics.
- [[Tag Composer]] - in-context binding reference (groups + search-bar bindings).
- [[Facet Editor]] - the most keyboard-driven page; full table + the no-mouse triage loop.
- [[Output Viewer]] - tree / thumb pane ping-pong.
- [[Danbooru Wiki]] - back/forward + smooth-scroll wheel handling.
- [[Settings]] - where the text-input guard makes typing in fields safe regardless of what shortcuts you might fire elsewhere.
