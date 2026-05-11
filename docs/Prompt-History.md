# Prompt History

A session-only log of every prompt pushed to ComfyUI. Inspect what was sent, replay it verbatim (same seed), save its full state as a named composer state, or restore back to the composer for a tweak-and-resend. Cleared on app close.

> **Screenshot suggestion:** the full page with a record selected on the left and the details pane on the right showing the meta header, positive prompt block, workflow vars, LoRAs, rules, replacement vars, and a row of entry thumbnails. Use as the page header image.

## Why it exists

The composer's Run button is one-shot - press it, the prompt vanishes from the UI and lives only as a ComfyUI queue entry. If you want to:

- **Re-send the exact same prompt** without re-typing or undoing intermediate edits;
- **Snapshot a configuration that produced a result you liked** into the saved states list;
- **Roll back the composer to whatever state produced a specific output**;
- or just **see what was actually sent** (post-pipeline, post-substitution),

...this page is the surface. Every queued prompt - composer Run iterations, batch iterations, even re-queues from this page itself - lands here as a row.

## Layout

The page is divided into a fixed-height header bar at the top and a horizontal splitter below it.

- **Header bar** (50 px, dark fill + bottom rule, same shape as the workflow editor) - title `PROMPT HISTORY`, subtitle `session only - cleared on app close`, and a `Clear` button.
- **Left column** (~280 px default, splitter-resizable) - the list of records, newest first.
- **Right column** (flex) - details for the selected record. Transparent background so it blends with the page.

When there are zero records, the splitter is hidden and a centered "No prompts queued this session." placeholder fills the area.

## Header bar

| Control | Effect |
| --- | --- |
| **Clear** | Drops every record from history and emits `cleared`. The right pane goes back to the empty placeholder. No confirmation dialog. |

`Clear` only wipes the history list; it does NOT touch saved composer states, ComfyUI's queue, or anything else.

## Left column - record list

One row per recorded prompt. Each row is a two-line label:

```
HH:mm:ss   <workflow name>
<n> var(s)  -  <m> LoRA(s)  -  <k> entr(y|ies)[  -  batch]
```

- `HH:mm:ss` - the wall-clock time the prompt was queued.
- Workflow name - from the workflow file the run used (`(unnamed)` if blank).
- The three counts come from the snapshot (workflow var count, LoRA count, active push count).
- A trailing `- batch` flag if the record was produced by the [[Workflow Editor]]'s batch runner.

Newest records sit at the top. The list highlights the currently-selected row with a left blue border.

> **Screenshot suggestion:** a list with maybe 10 records, with a mix of single composer Runs and a clump of batch rows.

### Selection persistence

When a new record arrives mid-session, the page prepends it but keeps your existing selection pointed at the same record (it shifts to row +1). This means you can sit on a record studying it while another run completes in the background without losing your place. The new record sits at row 0, visible at the top.

## Right column - details pane

Selecting a record populates the pane top-to-bottom. Every section is read-only inspection except the action bar.

### Action bar (top)

Three buttons, only enabled when a record is selected.

> **Screenshot suggestion:** close-up of the action bar with the three buttons + tooltips visible.

| Button | What it does |
| --- | --- |
| **Re-queue** | Sends the exact rendered JSON to ComfyUI again. Same seed. The replay is itself recorded as a new history row (with a fresh timestamp) so the log reflects every actual push. |
| **Save as state** | Forwards the record's snapshot into the composer's state manager under the name `History HH:mm:ss (<workflow>)`. Appears in the composer's STATES tab; survives across app restarts. |
| **Restore to composer** | Loads the record's snapshot back into the composer (active tags, weights, deactivated set, rules, replacement vars, workflow selection, workflow vars, LoRAs, pushed entries) and switches to the [[Tag Composer]] page. |

The crucial detail: **Re-queue uses the rendered JSON**, not the snapshot. Whatever seed was substituted at the time of the original run is baked into that JSON, so re-queue is byte-for-byte identical to the original push. Increment / Randomize seed modes don't advance on a re-queue.

### Meta header

A few key-value rows at the top of the details:

| Row | Value |
| --- | --- |
| **Queued** | Full date + time. |
| **Workflow** | Workflow display name. |
| **Source** | Either `composer Run`, or `batch  -  <entry title>` (with the iterated entry's title resolved against the [[Tile View]] model; falls back to `entry id <N>` if the entry was deleted post-run). |

### Positive prompt

The full post-pipeline positive-prompt string in a dark code block. Selectable so you can copy it out to a tag analyzer / Danbooru search / wherever.

For an upscale workflow that drops `__positive__` entirely, this is the prompt the composer *would* have substituted - the JSON might not have used it.

### Workflow variables

One row per declared workflow variable, in the order they appeared at queue time. Each row shows `placeholder -> formatted value` where the value's shape depends on the type:

| Var type | Formatted value |
| --- | --- |
| Seed | The integer seed used. |
| String | The replacement text (or `(empty)`). |
| Integer / Float | The numeric value. |
| Dir Search | The selected file (or `(none)`). |
| Latent Size | `WxH`. |
| Image | First 8 chars of the image uuid + `...` (or `(none)`). |
| Wildcard | `[N wildcard line(s)]` - the actual pick lives in the positive prompt. |

If no variables were declared, the row shows `(no workflow vars)`.

### LoRAs

One line per LoRA in the stack actually sent (post-heal). Format: `<file basename>  -  model <m> / clip <c>`. Empty stack shows `(no LoRAs)`.

These reflect what ComfyUI was told to load, including any per-run strength tweaks - not the same thing as "what LoRAs are currently active on the tile view".

### Rules

Lists every **enabled** rule at the time of the run, with its `Add` / `Replace` arguments in brackets when present:

```
RemoveSearchOnlyTerms
NoHeadwear   [no headwear]
ForceBreastSize   [gigantic breasts]
```

Disabled rules are skipped to keep the section focused on what was actually contributing to the pipeline. `(no enabled rules)` if everything was off.

### Replacement variables

User-defined `$name$` substitution variables and their values at the time of the run. Shows `$name$ -> value` per row, `(empty)` for blank values. `(no variables)` if the composer's variable index was empty.

### Active entries

The entries whose tags were pushed into the composer at queue time, rendered as a width-responsive grid of thumbnail tiles. The grid reflows on splitter drag / window resize - tiles never spill horizontally.

> **Screenshot suggestion:** the active-entries grid with 6+ tiles, ideally spread across two rows, in a wide pane so the responsive column count is visible.

Per tile:

- **120 x 150 image preview** - center-cropped from the entry's first image. Asynchronously decoded on a worker pool, placeholder rect shown until ready.
- **Title** below (or `(untitled)` / `uuid xxxxxxxx...` fallbacks for entries without titles / missing entries).
- **Tooltip** with the title and the per-image tag count.
- **Click** - jumps to the [[Tile View]], clears any active search query, and selects the entry.

The thumbnail cache is page-wide and persists across selection changes, so revisiting the same entry across multiple history records doesn't re-decode the image.

#### Batch records: synthesized entry push

When you Run a batch from the [[Workflow Editor]], the iterated entry's tags **don't** go through the composer's push system - they're unioned transiently into the positive prompt. To keep the history honest, the page synthesizes an `EntryPush` for the iterated entry on its way into the record:

- The batch entry is **prepended** to `activePushes` so it appears first in the grid.
- Its tags are unioned into `snapshot.activeTags` so a `Restore to composer` from the batch record reproduces the effective tag set, not just the composer state at the time the batch was triggered.

The composer-state snapshot reflects *post-batch-injection* truth, so all three action buttons work consistently on batch records.

## Session lifecycle

Records live in `core::PromptHistory`, owned by `AppMainWindow`. It's a `QList<PromptRecord>` in memory - nothing is written to disk, nothing is read back on startup. The history is empty every time the app launches.

If you want a prompt to survive a restart, **Save as state** is the answer. The state is written to `data/states/<id>/state.json` and loads back into the composer's STATES tab on next launch.

## Async thumbnails

Decoding entry images with `Qt::SmoothTransformation` on the GUI thread was visibly laggy for records with several active entries. Now:

- Each thumbnail dispatches to the global `QThreadPool` via `QtConcurrent::run`.
- A `QFutureWatcher` parented to the page receives the decoded `QImage` back on the GUI thread and pushes it into the awaiting `QLabel`.
- A `m_pendingThumbs` map dedupes - multiple tiles waiting on the same `(uuid, imageFileName)` key share one worker.
- `QPointer`-wrapped label refs in the pending map mean tiles destroyed mid-decode (e.g. by selecting a different record) are silently skipped instead of crashing.

You see the placeholder rect first; tiles pop in as their workers finish. The cache lives for the whole session, so re-selecting a record you've already viewed shows tiles instantly.

## Tips

- **Re-queue is your "make another one with the same seed" button.** Press it once and the same prompt fires; press it three times and you've sent three identical prompts. Useful when you want a specific output you almost got but ComfyUI sampled away from.
- **Save as state captures everything.** Including the LoRAs, the workflow var values, the active tags, the deactivated tags, the weights, and the rule enable state. A saved state is a faithful "play that prompt from this composer state" button.
- **Restore to composer is non-destructive only to the history.** It overwrites the live composer state, so save the current state first if you want to keep it.
- **Clear button has no undo.** It nukes the in-memory list. If you've been building up records during a long session, save the ones you care about as states *before* clicking Clear.
- **Click a thumbnail to triage from the entry side.** Especially useful when you want to mark up the entry that produced a result (tag it, comment it, push its LoRA differently for the next run).
- **The list keeps the same record selected when new ones arrive.** Sit on a record while a multi-prompt batch runs in the background - your cursor doesn't move.
- **Batch rows are flagged.** The `- batch` suffix in the list row makes it easy to skim past iteration noise when you're looking for a specific composer Run.
- **Workflow editor's Run Batch records every iteration.** A 5-entry batch with `count = 3` produces 15 history rows. Use the workflow name + timestamps to scope them visually.

## See also

- [[Tag Composer]] - the composer state that snapshots back here.
- [[Workflow Editor]] - the batch runner that produces `batch` flagged records.
- [[Tile View]] - thumbnail clicks land here.
- [[ComfyUI Integration]] - the run/batch dispatch flow that produces records in the first place.
