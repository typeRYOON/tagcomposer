# Workflow Editor

The page that wires up everything between the composer prompt and the actual JSON sent to ComfyUI. Three responsibilities, one per column:

| Column | What it does |
| --- | --- |
| **Workflow variables** | Declare and configure the placeholders (`__seed__`, `__steps__`, ...) the selected workflow JSON expects. Tokens defined here get substituted at queue time. |
| **LoRA stack** | View and tune the active LoRA stack - the same stack the composer's Run uses. Strength edits write back to the source entry so they survive across runs. |
| **Batch** | Fire the current composer prompt across every entry matching a query (same grammar as the tile-view search bar). |

> **Screenshot suggestion:** the whole page after selecting a workflow that has several variables defined, with two LoRAs active, and a batch query populated with a few matched entries on the right. Use as the page header image.

## Layout

Three **equal-width** columns, edge-to-edge. Each column has a 50 px header (dark bar with a bottom rule) and a scrollable body below it.

There's no save button anywhere on the page - every edit persists to disk immediately. The active workflow is whatever's selected on the [[Tag Composer]]'s WF tab; the title under "WORKFLOW VARIABLES" shows the workflow name (or "No workflow selected") and updates when you change the selection in the composer.

## Left column - Workflow Variables

### Header

- **WORKFLOW VARIABLES** title + the active workflow's name as a subtitle.
- An **open-external icon** that opens the workflow's `.json` file in your system editor. Useful for inspecting the raw JSON to figure out which placeholders the template references.
- A **`+ Add Variable`** button. Clicking opens an 8-option menu (Seed / String / Integer / Float / Dir Search / Latent Size / Image / Wildcard). Picking one appends a new card with a sensible default placeholder (`__seed__`, `__string__`, etc.) - rename it to whatever your JSON expects.

When no workflow is selected the left column shows a hint; everything's read-only until the composer picks one.

### Variable cards

One card per declared variable. Each card has a header row (placeholder edit + type badge + `×` remove button) and a type-specific body below it.

The placeholder string is what gets substituted in the workflow JSON. Two exceptions:

- **Wildcard** variables don't have a substitution placeholder - they merge into `__positive__` along with the composer's tags. The placeholder field for a wildcard is **label-only** ("Name (label only)" placeholder text) - it's just a name you see in this page.
- **LatentSize** variables use **two separate tokens** for width and height (configured in the body), not a single placeholder. The main placeholder field is also label-only.

> **Screenshot suggestion:** a column with one card of each type stacked, so each type's body shape is visible.

### Variable types in detail

#### Seed

```
+--------------------------------+
|  __seed__       [SEED]    [×]  |
|                                |
|  ( ) Fixed  ( ) Increment      |
|  (•) Randomize                 |
|                                |
|  Value: 1234567890             |
+--------------------------------+
```

Three modes (radio buttons):

- **Fixed** - the saved value goes into the JSON every time. Reproducible runs.
- **Increment** - the value is bumped by one every iteration (and saved back), so each run's seed is sequential.
- **Randomize** - a fresh 63-bit random seed per iteration; the actually-used value is written back to the editor after each run so you can see what was sent.

The value field shows the current seed as a signed integer.

#### String

A single line edit. Whatever you type replaces the placeholder verbatim (JSON-string-escaped) at queue time. Common uses: `__negative__` for a fixed negative-prompt body, `__sampler__` for a sampler name, etc.

#### Integer / Float

Spin boxes. Integers cover the full `int` range; Floats use 4 decimal places and range `±1e9` with 0.1 default step.

#### Dir Search

Used for "pick a model checkpoint" / "pick a VAE file" kinds of variables.

```
+----------------------------------------+
|  __ckpt__               [DIR SEARCH]   |
|                                        |
|  [D:/models/checkpoints       ] Browse |
|  [.safetensors                       ] |
|  [Filter by name...                  ] |
|                                        |
|  +----- subfolder/file.safetensors ---+|
|  |  another.safetensors                ||
|  |  ...                                ||
|  +-------------------------------------+|
+----------------------------------------+
```

- **Search directory** + Browse button.
- **Extension whitelist** (comma-separated, e.g. `.safetensors, .ckpt`). Empty means "all except `.sha256`" so the default behavior is usually right.
- **Filter** (transient, not persisted) - narrows the list by filename substring.
- **File list** - shows up to 500 matching files, click to pick. The selection is the value substituted into the JSON (path relative to the search directory, with `/` swapped for `\\` for ComfyUI's path conventions). The currently-selected file is bolded and green.

Safety: the root of the filesystem is refused as a search directory. Pick a model folder, not `D:\`.

#### Latent Size

For "image dimensions" variables. The list of size presets comes from `data/system/latent_sizes.txt` (format: `<width> <height>` per line, `#` for comments). Each entry shows as `widthxheight (ratio)`.

```
+--------------------------------------+
|  __latent__         [LATENT SIZE]    |
|                                      |
|  +-- 896x1088 (0.82) -+   +--------+ |
|  |  1024x1024 (1.00)   |  |  ratio | |
|  |  1216x832  (1.46)   |  |  preview |
|  |  ...                |  |        | |
|  +---------------------+   +--------+ |
|                                      |
|  Width token:  [__latentw__       ]  |
|  Height token: [__latenth__       ]  |
+--------------------------------------+
```

A live ratio preview to the right of the list shows the aspect ratio as a small rectangle so you can eyeball portrait vs landscape vs square at a glance.

Two configurable **JSON tokens** at the bottom: the width token and the height token. These are substituted into the workflow JSON as **raw integers** (not strings), so they go into wherever your workflow's `EmptyLatentImage` (or equivalent) node reads its dimensions from. The placeholder field on the card header stays as a free-form label.

#### Image

For workflows that take an image input (img2img, controlnet, inpainting reference, ...). The bound image lives in `data/workflow_inputs/` (the `WorkflowInputCache`) and gets re-uploaded to ComfyUI lazily before any run that uses it.

```
+----------------------------------------+
|  __input_img__              [IMAGE]    |
|                                        |
|  +--+   <name of bound image>          |
|  |  |   • cropped 768x768              |
|  |  |   [Browse] [Edit] [Clear]        |
|  +--+                                  |
+----------------------------------------+
```

- **Thumbnail (96 x 96)** with drop-target for image files.
- **Browse / Edit / Clear** buttons. Browse opens a file dialog; Edit launches the in-app **ClipEditorDialog** (crop, mask, alpha trim); Clear unbinds and frees the cached file.
- The name label surfaces edit state ("cropped 768 x 768", "mask 1024 x 1024") so you can tell at a glance that the upload won't be the raw source.

Edits are cached separately from the source image - the edited PNG is rendered (and re-rendered on edit-changes) into the input cache; uploads use the edited version.

#### Wildcard

Plain multi-line edit, one tag-set per line. At each iteration of Run, a random line is picked and merged into the positive prompt - rules and replacement vars apply to the picked tags the same way they apply to composer-added tags.

```
+----------------------------------------+
|  HairColors              [WILDCARD]    |
|                                        |
|  One slot per line. A random line is   |
|  picked for each prompt run and        |
|  merged into the positive prompt -     |
|  rules and replacement vars apply.     |
|  Commas split a line into multiple     |
|  tags.                                 |
|                                        |
|  | blue hair, blue eyes              | |
|  | red hair                          | |
|  | blonde hair, twintails            | |
+----------------------------------------+
```

Commas within a line split it into multiple tags, so `blue hair, blue eyes` is one slot that contributes two tags when picked.

Multi-run iteration (Run with `count > 1` in the composer) re-picks per iteration, so a 4-run batch over a 10-line wildcard fires four prompts each with a random pick.

Saves debounce 400 ms after the last keystroke - typing into the edit doesn't hit disk every character.

## Middle column - LoRA Stack

Shows the **currently-active** LoRA stack: a mirror of the orange-ringed entries in the [[Tile View]], in activation order. Subtitle in the header shows the count (`3 active`).

> **Screenshot suggestion:** a column with two or three LoRA cards stacked, the first one showing a real entry preview and the second flagging "(entry missing)".

### LoRA cards

```
+------------------------------------------+
|  +----+   #1   character_name_lora        |
|  | im |   Model  [0.85    ]               |
|  | g  |   Clip   [2.00    ]               |
|  +----+                                   |
+------------------------------------------+
```

Per card:

- **Position badge** (`#1`, `#2`, ...) - the slot index in the stack. Slot 1 substitutes into `__lora_name_1__` / `__lora_wt_1__` / `__lora_model_str_1__` / `__lora_clip_str_1__` in the workflow JSON; slot 2 into the `_2_` family, etc.
- **Image preview (80 x 100)** - the first image of the entry the LoRA is bound to. Placeholder if the entry has no image.
- **Filename** (basename of the LoRA file). Full path in the tooltip.
- **Model strength** spin (0.00 - 2.00, step 0.05).
- **Clip strength** spin (0.00 - 4.00, step 0.10).

Strength edits propagate **live** to three places:

1. The internal stack the page is rendering.
2. The bound entry's `LoraConfig` (persisted via `EntryModel::saveEntry`).
3. The `AppMainWindow`'s cached `m_activeLoraStack` via `loraStrengthsChanged`.

So the next composer Run uses the new values immediately. No need to re-toggle the LoRA on the tile.

If a LoRA in the stack can't be resolved to a source entry (e.g. the entry was deleted but the active LoRA stack still references it by sha256), the card shows "(entry missing)" and the spin boxes are disabled - there's nothing to save edits into.

### Adding / removing LoRAs

This page **only displays and tunes** the stack - it doesn't add or remove LoRAs. Activation happens in the [[Tile View]]: right-click a LoRA-bound entry tile -> Activate / Deactivate LoRA. The stack here updates instantly via the page's `setActiveLoraStack` signal.

## Right column - Batch

Fire a multi-entry run without manually pushing each entry to the composer. Same composer state, same workflow vars, same LoRAs - but the prompt is rebuilt **per-matched-entry** with that entry's tags unioned on top.

```
+----------------------------------------+
|  BATCH                                 |
|                                        |
|  Run the current composer prompt       |
|  across every entry matching the       |
|  query - same syntax as the tile-view  |
|  search bar.                           |
|                                        |
|  [entry query, e.g. "kantai, -nsfw"  ] |
|  3 entries matched                     |
|                                        |
|  +-------- Akagi (Kantai) ----------+ |
|  |  Kaga (Kantai)                   | |
|  |  Shoukaku (Kantai)               | |
|  +----------------------------------+ |
|                                        |
|  [▶ Run Batch]   Dispatched 9 prompts. |
+----------------------------------------+
```

### Query field

Same grammar as the [[Tile View]] search bar - see [[Search Queries]] for the full reference (tag prefix match, OR groups with `|`, `title:` / `lora:` / `sort:` / `images:` / `tags:`, `has:` / `missing:`, etc.).

Live preview: typing into the field is debounced 120 ms, then `EntryModel::filter` runs and the matched entries are listed below with an entry count label (`3 entries matched`).

The preview list is read-only - it's there to confirm what the query will actually batch over before you commit.

### Run Batch

Clicking the button (or pressing Enter in the query field) fires the run. The actual queue logic lives in `AppMainWindow::runBatch`:

1. Validate the workflow template (same checks as the composer's Run - see [[ComfyUI Integration]] **(stub)** for the rules). Missing image inputs, unused workflow variables, stray dunder tokens, or insufficient LoRA slots all block the run with a status-bar message.
2. Upload any image inputs that haven't been pushed to ComfyUI yet.
3. For each matched entry:
   - Skip if the entry has zero tags (nothing to add on top of composer state).
   - Build a per-entry LoRA stack: global active LoRAs + the entry's own LoRA (if any), then heal.
   - Run `composerPage->computePromptWithExtraTags(entryTags + wildcards)` to produce the per-iteration positive prompt.
   - Apply the workflow JSON, fill `__positive__` and the LoRA stack, queue.
   - Iterate `count` times (from the composer's count spinner).
4. Post-run, a summary lands in the status label next to the button: "Dispatched 12 prompts (4 entries x 3) - 1 skipped" or similar.

ComfyUI processes the queue serially on its end; the app is fire-and-forget after the dispatch loop completes.

### What gets recorded

Every queued prompt - one per (entry, iteration) - lands in the [[Prompt History]] with `batchEntryId` set to the iterated entry. The synthesized push (entry's image + its tags) is attached to the snapshot, so "Restore to composer" on a batch record reproduces the effective state that drove that one iteration. See [[Prompt History]] for details.

## Workflow JSON registration

This page doesn't list the available workflows - that's on the [[Tag Composer]]'s WF tab. To register a new workflow:

1. Drag a `.json` file onto the composer's workflow list. It's copied into `data/workflows/`.
2. Select it. The Workflow Editor's left column repopulates with whatever variables were already saved against it (empty for a fresh workflow).
3. Click `+ Add Variable` for each placeholder your JSON references. The composer's run validation will yell at you if a `__dunder__` token in the JSON has no matching declared variable.

## Tips

- **Match placeholder strings to your JSON.** A typo here means `applyToJson` won't replace the token at queue time, and the validation step will block the run. Use the open-external icon to grep the actual JSON for `__` patterns and copy them verbatim.
- **One variable, one purpose.** Don't reuse `__steps__` for both steps and CFG - pick distinct placeholder names. The composer's "unused variable" check (see [[ComfyUI Integration]] **(stub)**) only fires when a declared variable doesn't appear in the JSON at all; it can't tell you that two declared variables share a token.
- **Use Increment seeds for grids.** Set the seed mode to Increment and run with count=N to get N sequential seeds in one go - useful for sweeping a parameter while keeping seed drift comparable.
- **Latent Size's width/height tokens are integers.** Don't quote them in the JSON template; `applyToJson` substitutes raw numbers, not JSON strings.
- **Wildcards bypass the active set.** A wildcard's tags are merged into the positive prompt *at run time*, not added to the composer's `m_activeTags`. So you can't see them in the composer between Runs - check [[Prompt History]] to see what was actually picked for a given iteration.
- **Edit LoRA strengths here, not in the tile view's entry panel.** Both work, but doing it here gives you the whole stack side-by-side so you can balance interactions across multiple LoRAs in one place.
- **Batch with a narrow query first.** "kantai" might match 200 entries; you probably wanted a subset. Watch the live preview before clicking Run.
- **Sort on the batch query doesn't reorder dispatches.** The `sort:` clause sorts the *display* of matched entries, but `runBatch` iterates them in the model's filter order. If you need deterministic per-entry order, just rely on the default (creation time desc).

## See also

- [[Tag Composer]] - selects the active workflow and drives the per-Run pipeline.
- [[Prompt History]] - records every dispatched prompt (including batch iterations).
- [[Tile View]] - where LoRAs get activated; the stack mirrored here.
- [[Search Queries]] - the query grammar shared by the batch query field.
- [[Workflow Variables]] - **(stub)** - the cross-cutting reference for placeholder substitution rules and built-ins (`__positive__`, `__lora_*__`).
- [[ComfyUI Integration]] - **(stub)** - the run/batch validation rules and queue/interrupt semantics.
