# TagComposer

A Qt6 / C++23 desktop app for managing tags, LoRAs, and prompt state for AI image generation. Integrates with [ComfyUI](https://github.com/comfyanonymous/ComfyUI) over its HTTP + WebSocket API: queue prompts, monitor jobs, surface generated images, and keep a session history.

This wiki is the user-facing reference. Click into a page for the full feature list of that part of the app.

## Pages

The app is divided into nine pages, navigated via the left-edge nav bar:

| Page | What it does |
| --- | --- |
| **Home** | Landing screen. App version, quick links, update banner. |
| [[Tile View]] | The entry browser: search, select, edit metadata, push to composer. The main day-to-day surface. |
| [[Tag Composer]] | Build a prompt from tags, rules, variables, and workflow state. Hit Run to queue against ComfyUI. |
| [[Workflow Editor]] | Edit the variable bindings for each workflow JSON template. Configure LoRA stacks, latent sizes, and image inputs. |
| [[Facet Editor]] | Define per-tag metadata ("facets") - which character, copyright, style a tag belongs to. Drives quick-add menus and triage views. |
| [[Prompt History]] | Session log of every prompt pushed to ComfyUI. Inspect, re-queue (same seed), save as a state, or restore to the composer. |
| [[Output Viewer]] | Browse the ComfyUI output folder. Tree + thumbnail grid; click to open in the system viewer. |
| [[Dataset Helpers]] | Tag-cluster mining, auto-tagging, batch tag editing, and crawl-style image collection. |
| [[Danbooru Wiki]] | In-app viewer for danbooru.donmai.us wiki pages and tag aliases. |
| [[Settings]] | Folders, ComfyUI host, quick-facet bindings, theme accents, and friends. |

## Reference

Cross-cutting docs that aren't tied to one page:

- [[Search Queries]] - the full query grammar shared by the tile-view search bar (and anywhere else `EntryModel::filter` is invoked).
- [[Tag Composer - Rules]] - the `rules.fct` grammar: match expressions (`anyTag(facets:...)`, `anyTag(name:"...")` globs, AND/OR/NOT), all five action types (`skip`, `delete`, `add`, `replace`, `flag`), and the `force` flag.
- [[Tag Composer - Groups]] - the `groups.fct` grammar plus the routing-facet (`rBody`, `rClothing`, ...) convention that pairs with leaf facets to drive the composer's category buckets.
- [[Workflow Variables]] - placeholder substitution rules, built-ins (`__positive__`, `__lora_*__`, ...), and per-type behavior.
- [[ComfyUI Integration]] - connection setup, queue/interrupt semantics, image input upload flow, and the run/batch validation rules.
- [[Keyboard Shortcuts]] - app-wide and page-local key bindings.

## Concepts

A few terms recur across the docs:

- **Entry** - a folder under `data/entry/<uuid>/` holding one or more images plus their tag lists, a title, free-form comment, and (optionally) a bound LoRA.
- **Composer push** - sending an entry's tag list up into the composer's active set. Pushed entries paint a green inset border in the tile grid.
- **LoRA active** - LoRAs the composer is currently weighting into the prompt. Shown as an orange outer ring on the tile (or a small badge if the entry has a LoRA but isn't using it).
- **Workflow** - a ComfyUI workflow JSON (.json) plus the user-defined variables that substitute into it at queue time.
- **State** - a named snapshot of the composer + workflow vars + LoRA stack. Stored under `data/states/`. Re-loading a state replays the exact prompt-building inputs.
- **Facet** - a per-tag classification (character, copyright, style, etc). Used by the composer for rule-engine inputs and by the dataset helpers for triage.

## Data layout

Everything the app writes lives next to the executable in `data/`:

```
data/
  entry/<uuid>/        per-entry folder: images, tag-list files
  states/<id>/         saved composer states (one folder per state)
  workflows/           workflow JSON templates
  workflow_inputs/     uploaded image-input cache + masks
  collections/         dataset-helper image collections
  models/              auto-tag ONNX models
  system/
    danbooru.csv       tag definitions / categories
    facets.fct         facet schema
    tag_definitions.fct  per-tag facet bindings
    rules.fct          composer rules
    groups.fct         composer tag groups
    vars.fct           composer replacement vars
    workflows.json     workflow registry
    settings.json      app settings
    session.json       last session state
    ...
```

## Conventions

- Tags are stored Danbooru-style with underscores (`aqua_eyes`) for indexing, but rendered to the user with spaces (`aqua eyes`).
- IDs are forever-monotonic: deleted entries leave their numeric id retired, never reclaimed.
- Backwards-compat reads (legacy CamelCase enum names, missing fields) are tolerated; writes always use the current canonical form.
