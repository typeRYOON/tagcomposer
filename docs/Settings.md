# Settings

App-wide configuration and a few maintenance actions. All settings persist to `data/system/settings.json` and most apply live - the exceptions (gradient start, gradient alpha, tile title color) are flagged as restart-only because they're baked into pixmaps at startup.

> **Screenshot suggestion:** the page scrolled to the top showing the APPEARANCE section + a partial BACKENDS section with ComfyUI enabled and a green connected dot. Use as the page header image.

## Layout

A single vertical scroll body with six labeled sections, each rendered as a dark "group" card with its own field grid + hint lines. The page has no header bar (it's the only top-level page without one - settings are themselves the metadata).

Section order:

1. **Appearance** - tile / SFX / danmaku visuals.
2. **Backends** - ComfyUI connection + LoRA folders.
3. **Facets** - quick-add facet bindings + canonical-file shortcuts + purge actions.
4. **Data** - import / export entries.
5. **Input Images** - workflow image cache cleanup.
6. **Log** - read-only session log + version footer.

A field's value commits when you `Tab` out of it, focus-loss, or `Enter` the line edit / spinbox. Most fields then fire `settingsChanged`, which `AppMainWindow` handles by re-applying every relevant subsystem (re-connecting the ComfyUI client if the host changed, pushing new quick-facet labels into the right-click menus on every page, etc.).

## Appearance

### Background animation (danmaku)

Checkbox. Toggles an animated scrolling-text overlay behind every page. Off by default. The text comes from `data/system/danmaku.txt`. Sits behind every page widget so it never interferes with input.

### Tile gradient

Three controls govern the [[Tile View]] tile appearance:

| Field | Range | Purpose |
| --- | --- | --- |
| **Tile gradient start** | 0.00 - 1.00 | Where the bottom-to-top fade begins. 0 = fade covers the whole tile; 1 = no fade. Default 0.6. |
| **Tile gradient opacity** | 0 - 255 | Alpha of the bottom edge of the fade. Default 180. |
| **Tile title color** | hex picker | Tile title text color, sampled from the swatch button. Defaults to white. |

**Restart-only.** These three values get baked into the pre-rendered tile pixmaps at app start; the [[Tile View]] reads them once and never re-reads. Save, restart, see the new look.

### SFX volume

Slider, 0.0 - 1.0. Live-applied to every `QSoundEffect` clip in `core::SoundPlayer` (the "ok" / "finish" effects fired on certain user actions and on ComfyUI run completion).

## Backends

### ComfyUI

Single checkbox at the top of the group enables/disables the connection. The detail fields below are visible regardless but only have any effect when the checkbox is on.

A status row to the right of the checkbox shows the current connection state:

| Indicator | Means |
| --- | --- |
| Grey dot + "disabled" | Master toggle is off. |
| Yellow dot + "connecting..." | WS is opening. |
| Green dot + "connected" | WS handshake completed; ready to queue prompts. |
| Red dot + error text | WS dropped or failed to open. The error message is the underlying network error. |

The state is pushed in by `AppMainWindow` via `setComfyStatus` whenever the WS emits `connected` / `disconnected` / `connectionError`.

The fields:

| Field | Stored as | Notes |
| --- | --- | --- |
| **Server address** | `comfyUiServerAddress` | `host:port`, no scheme. Default `127.0.0.1:8188`. |
| **API key** | `comfyUiApiKey` | Optional. Password-masked (`QLineEdit::Password` echo mode). Sent as `extra_data.api_key_comfy_org` on every POST when set. Leave blank for unsecured local instances. |
| **Output folder** | `comfyUiOutputFolder` | Path pattern, optionally with `{yyyy-MM-dd}` (or any other `QDateTime::toString` spec) for date-based subfolders. Drives the [[Output Viewer]]. Browse button picks an existing folder. |
| **Temp folder** | `comfyUiTempFolder` | Flat folder ComfyUI writes its in-progress decode images to. Drives the composer's inline preview tile after each completed prompt. |
| **LoRA folder** | `loraBaseDir` | Primary LoRA root - typically `<ComfyUI>/models/loras`. Used to resolve LoRA file paths bound to entries. |
| **LoRA test folder** | `loraTestDir` | Optional secondary LoRA root. Drops from this folder are recognised as already-placed and skip the LoRA import dialog. Match this to ComfyUI's `extra_model_paths.yaml` entry if you have one. |
| **LoRA defaults** | `defaultLoraModelStr` / `defaultLoraClipStr` | Model + Clip strengths applied to a newly-added LoRA. Existing entries keep whatever they were saved with. Spin boxes (Model 0.0-2.0 step 0.05, Clip 0.0-4.0 step 0.10). |
| **Input folder** | `comfyUiInputFolder` | Optional ComfyUI `input/` directory. When set and writable, image inputs copy directly there (fast path) instead of going through HTTP multipart upload. |

The full ComfyUI integration story (what the WS delivers, which endpoints get hit, the run-block validation rules) lives on [[ComfyUI Integration]].

### Connect button

Below the fields. Reapplies the current settings and reconnects the WS - equivalent to toggling the checkbox off + on. Useful after editing the server address or API key without changing the enable state.

## Facets

### Quick-add facet bindings

Four line edits binding the four "Quick add as ..." context-menu entries to facet names. Each one shows the LoRA-training convention as a placeholder (`rCharacter`, `rCopyright`, `rTriggerWord`, `rStyle`), but you can put any facet name from `facets.fct` here.

| Field | Stored as | Driven menu |
| --- | --- | --- |
| Quick character facet | `quickCharacterFacet` | "Quick add as character" |
| Quick copyright facet | `quickCopyrightFacet` | "Quick add as copyright" |
| Quick trigger word facet | `quickTriggerWordFacet` | "Quick add as trigger word" |
| Quick style facet | `quickStyleFacet` | "Quick add as style" |

The menu entry is **hidden** when the corresponding binding is empty - so leaving a field blank removes that menu item from every right-click menu in the app. Useful when your facet schema doesn't have one of these categories.

Where the menu appears: [[Tag Composer]] tag rows, [[Tile View]] entry-panel tag rows, [[Facet Editor]] (indirectly - it's the destination for "Edit facets"), [[Dataset Helpers]] tag cluster result rows.

### Purge tag definitions

```
Purge stale tag definitions
```

Drops every entry from `tag_definitions.fct` that meets BOTH of these criteria:

- Has zero facets defined.
- Isn't in the Danbooru tag CSV AND isn't used by any [[Tile View]] entry's tag list.

In-memory only - the rewrite happens on next app shutdown when `saveDefinitions` runs. Use after a long session of triaging facets to clean up the file.

### Purge unknown facets

```
Purge unknown facets
```

Walks every tag definition and strips any facet entries whose name isn't in the current `facets.fct` schema (case-sensitive). Catches leftovers from hand-edited `tag_definitions.fct` or renamed quick-facet settings. Like the previous action, in-memory; persists on shutdown.

### Open canonical files

Three buttons that open files in your system editor:

| Button | File | Notes |
| --- | --- | --- |
| **Open `danbooru.csv`** | `data/system/danbooru.csv` | The tag-with-category list driving autocomplete + the category-color dots. Format: `tag,category,count,wrong` (the same header is written if the file doesn't exist yet). Restart to apply. |
| **Open `groups.fct`** | `data/system/groups.fct` | The composer's category bucketing rules. See [[Tag Composer - Groups]]. |
| **Open `tag_definitions.fct`** | `data/system/tag_definitions.fct` | Per-tag facet mappings. **Edit only while the app is closed** - it's rewritten on shutdown, so live edits will be overwritten. |

`facets.fct` (the schema) is opened from the [[Facet Editor]]'s header buttons; it's not duplicated here because reload-friendly editing is part of the facet editor's normal flow.

## Data

### Export / Import entries

```
Export entries...   Import entries...
```

Both buttons open a modal dialog for the operation.

- **Export entries** lets you pick a folder, choose which entries to ship, and writes the selection (plus any tag definitions referenced by those entries' tags) into a portable folder layout. The output is a self-contained drop suitable for moving between machines or backing up specific entries without snapshotting the whole `data/entry/` tree.
- **Import entries** reads a folder previously produced by Export, merges entries into the live model (duplicate entries are skipped by UUID), and offers a per-conflict choice for tag-definition collisions (skip / merge / overwrite), plus a facet-mapping table for any source facets your local schema doesn't recognize.

Neither operation touches workflows, states, or settings - it's purely entries + their tag-definition dependencies. The full dialog reference (mapping table, conflict modes, scan vs apply, on-disk layout) lives on [[Import Export Dialogs]].

## Input Images

```
Clear unused inputs
```

Sweeps `data/workflow_inputs/` (the [[ComfyUI Integration]] image input cache) and removes any cached file - main image, derivative edit variants under `_edited/`, and masks under `_masks/` - that no current `WorkflowVar` references. The sweep walks every workflow's variables plus every [[Tag Composer]] saved state's referenced image vars, so anything in active use is preserved.

Useful after deleting a workflow, replacing the image input on a workflow var, or restoring a state that referenced an image you've since changed. The status bar reports counts (`Removed N image(s), M mask(s), K render(s)`).

## Log

A read-only `QPlainTextEdit` at the bottom of the page, sized to ~200 px tall. Shows the rolling session log (capped at 5000 lines via `QPlainTextEdit::setMaximumBlockCount`).

On open, the log is seeded with the entire history that `utils::Logger` has accumulated since app launch - so opening the page mid-session catches you up. New log entries append in real time via `appendLogMessage` (called from `Logger::messageLogged`).

The log captures:

- Network errors (ComfyUI WS / HTTP failures, Danbooru wiki fetches).
- Upload failures.
- Workflow JSON parse failures.
- A few diagnostic prints from corner cases that don't warrant a popup.

It's the place to look when something didn't visibly work and the status bar's transient message is gone.

## Version footer

Below the log, a small centered row with the taskbar icon and `TagComposer vX.Y.Z`. The version string comes from `utils::APP_VERSION`.

## Tips

- **Most fields are live.** Edit the API key, click out of the field, the next `/prompt` POST uses the new key. No "Save" button is needed (and none is wired up).
- **The three appearance fields aren't live.** Tile gradient start / opacity / title color are baked into the pre-rendered tile pixmaps at startup. Restart to apply.
- **Empty quick-facet bindings hide menu entries.** If you only use `character` and `style` facets, blank out the copyright and trigger-word fields and those two menu items disappear app-wide.
- **Lora test folder isn't really "test".** It's a secondary LoRA root that gets the same recognition as the primary - dropped files from this folder skip the import dialog. Use it for any second LoRA location (e.g. one shared with another generation tool).
- **API key is password-masked.** The field hides what you type. If you need to verify what's stored, the value is in `data/system/settings.json` (plain text on disk).
- **Purge actions are non-destructive on disk until shutdown.** Both purges work in-memory and only land on disk during the next clean app exit. If you regret a purge mid-session, force-quit the app to keep the on-disk file untouched.
- **The log is your post-incident view.** Network failures, parse errors, and upload retries all land here. When the status bar's transient hint has scrolled away, this is where to look.
- **Settings stored elsewhere.** Per-page sticky state (auto-tagger threshold, collector poll interval, tag editor folder, etc.) lives in their respective panels - those still persist via `AppSettings` but aren't surfaced here.

## See also

- [[ComfyUI Integration]] - what every ComfyUI field actually does at run time.
- [[Tile View]] - what the appearance gradient / title color settings affect.
- [[Facet Editor]] - where the quick-facet bindings end up being exercised.
- [[Tag Composer]] - where the right-click quick-add menu lives.
- [[Tag Composer - Groups]] / [[Tag Composer - Rules]] - the files behind the "Open ..." buttons.
- [[Dataset Helpers]] - shares the auto-tag / collector settings (configured on those pages, not here).
