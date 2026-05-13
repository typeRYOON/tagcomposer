# Civitai article

Long-form copy for a Civitai article post. Civitai renders Markdown. Replace `[LINK]` with the repo / download page; drop screenshots in at the marked spots.

---

# TagComposer: a desktop workspace for tags, LoRAs, and prompts that drives ComfyUI

If you generate AI images with ComfyUI and you've ever found yourself juggling text files of tags, a folder of LoRAs you can't remember the trigger words for, and a dozen browser tabs of Danbooru wiki pages - this is the tool I built to make that part bearable.

**TagComposer** is a Windows desktop app (Qt6 / C++23) that sits in front of a running ComfyUI instance. ComfyUI does the actual generation; TagComposer is the workspace where you organize what goes into the prompt and watch what comes out.

> *[Screenshot: the full app - tile view on the left, entry panel on the right, nav rail. Use as the article header image.]*

## The thing that makes it different: facets

Most prompt tools treat tags as flat strings. TagComposer lets you attach **metadata** - "facets" - to individual tags. A tag like `red hair` carries a routing facet (`rBody` - "this is a body trait") plus leaf facets (`Hair`, `Color`). A character name carries `rCharacter`. A working tag like `wip` carries `SearchOnly`.

Once your tags have that metadata, two things become possible:

**1. Rules.** You write small `match -> action` rules over the facets:

- `anyTag(facets: SearchOnly) -> delete` - strip working/search-only tags before they reach the prompt.
- `anyTag(facets: Headwear) OR anyTag(facets: Headpiece) -> replace("no headwear")` - negate a whole category with one rule.
- `NOT anyTag(facets: Eyes, Color) -> add("blue eyes")` (with "force" on) - inject a default when something's missing.

The composer runs your active tags through these rules every time the prompt changes, live. It's a tiny prompt-aware transform layer you configure once and forget.

**2. Groups.** You define category buckets by facet combination - `Body/Hair` = tags with `rBody, Hair`; `Clothing/Headwear` = tags with `rClothing, Headwear`; etc. The composer displays your prompt bucketed into these readable groups instead of one flat blob, so a 60-tag prompt is actually navigable.

> *[Screenshot: the composer center column with several category groups, a few tag rows showing the colored dots / "?" badges / weight spinners.]*

The catch, to be upfront about it: tags don't come pre-classified. You have to populate the facet metadata. There's a dedicated workflow for it - the composer flags every tag that doesn't have facets yet, the Facet Editor lets you blow through that list fast (pick a tag, click the facet pills, Save, auto-advances to the next), and a right-click "Quick add as character/copyright/..." gives you a one-click shortcut for the obvious cases. But it's real upfront work, and worth saying so.

## The rest of it

### Entry library

An "entry" is a folder: one or more images, their tag lists, a title, a notes field, an optionally-bound LoRA. One entry per character, concept, style reference, whatever you organize by. The tile view is a fast scrolling grid of them with a real query language in the search bar:

- `red hair, blue eyes, -loli` - AND of tags, with negation.
- `taihou|st. louis|essex` - OR across groups.
- `red hair, title:azur | blue hair, lora:nai` - mix per-group title/lora filters and OR groups.
- `has:lora`, `missing:title`, `images:0`, `tags:>50`, `tags:<10` - presence and count predicates for triage.
- `sort:title:asc`, `sort:tags:desc` - sorting.

> *[Screenshot: the tile view with the search bar showing a non-trivial query and a grid of entry tiles, some with the green composer-active border, some with the orange LoRA ring.]*

### Prompt composer

Push entries into the composer (or type tags directly), and it builds the prompt: tags -> `$var$` expansion -> facet lookup -> rules -> category bucketing. Per-tag weights, tag deactivation (kept visible, struck through, excluded from output), 50-deep undo/redo, replacement variables, a live inline preview that fills in with the generated image after a run.

### ComfyUI integration

Select a workflow JSON file (drag-drop to register it). Bind its `__placeholder__` tokens to typed variables in the Workflow Editor:

- **Seed** - fixed / increment-per-iteration / randomize (and the editor shows you the seed that was actually used).
- **String / Integer / Float** - the obvious.
- **Dir Search** - browse a model folder, pick a checkpoint/VAE file.
- **Latent Size** - pick from a preset list, with a live aspect-ratio preview, substituted as raw integers into your EmptyLatentImage node.
- **Image** - bound image input for img2img / ControlNet / inpaint, with a built-in crop/mask/trim editor.
- **Wildcard** - one tag-set per line, a random line picked per iteration and merged into the prompt.

Built-ins (`__positive__`, the `__lora_*__` family) are handled automatically. There's a validation pass before every Run that catches typo'd tokens, unused declared variables, and missing LoRA slots, so you get a clear "blocked - here's why" instead of a confusing ComfyUI error.

Hit Run, watch the WebSocket: sampler-step progress in the status bar, in-progress preview images in the composer tile, the final output pulled back when the queue decrements. Shift+E to run, Shift+R to interrupt, Shift+Alt+R to clear the pending queue.

> *[Screenshot: the workflow editor's three columns - variables on the left, the LoRA stack in the middle, the batch panel on the right.]*

### LoRA management

LoRAs bind to entries. The Workflow Editor's middle column shows the active stack with per-LoRA model/clip strengths you can tune live (they write back to the entry and propagate to the next run). File paths auto-heal against your configured LoRA roots, so moving a folder in settings re-roots everything for free. The stack feeds the workflow's `__lora_name_N__` / `__lora_model_str_N__` / etc. slots.

### Prompt history

Every prompt you push - composer Run iterations, batch iterations, even re-queues - is logged for the session. For any record you can:

- See the actual positive prompt, the workflow var values used, the LoRAs, the active rules, the active entries (with thumbnails).
- **Re-queue** it - byte-for-byte the same JSON, so the same seed.
- **Save as state** - the full composer + workflow + LoRA snapshot, into the saved-states list, surviving restarts.
- **Restore to composer** - roll the whole composer back to whatever state produced that output.

> *[Screenshot: the prompt history page with a record selected, the details pane showing the prompt block + vars + LoRAs + entry thumbnails.]*

### Dataset helpers

A five-tab section for the dataset side of the workflow:

- **Tag Cluster** - mine Danbooru's API for tags that co-occur with a seed tag, ranked by PMI (so popular-everywhere tags get filtered out and the genuinely-associated ones float up). Copy the result, or turn it into a new entry seeded with those tags.
- **Auto-collect** - point it at your downloads folder; new images get perceptual-hash-deduped and routed into a named collection folder. Near-duplicates go to the recycle bin.
- **Auto-tagger** - run a wd14-style ONNX tagger over a folder, write `.txt` sidecars. Threshold, cooldown, recursive, move-or-copy.
- **Tag Editor** - hand-tune those sidecars one image at a time: big preview, tag textarea with Danbooru autocomplete, multi-color search highlight, fast prev/next navigation.
- **Batch Edit** - bulk operations across a folder of sidecars: remove tag, remove first, prepend, append, log tag frequencies.

The handoff buttons stitch collect -> auto-tag -> tag-edit -> batch-edit without leaving the page. The sidecars are in the standard `<image>` + `<image>.txt` layout, so the folder is training-ready as-is.

### And the small stuff

- **In-app Danbooru wiki** - right-click any tag, read its wiki page with example post thumbnails, follow links, back/forward history. No browser tab.
- **Output viewer** - browse ComfyUI's output folder (tree + thumbnail grid), with date-stamped subfolder support.
- **Import/export** - move a curated set of entries (plus their tag definitions) between machines as a portable folder, with a facet-mapping table for reconciling different schemas.
- A full [wiki]([LINK]) covering every page in detail.

## Honest scope

- **Windows-first.** Qt6 / C++23, bundled OpenCV, optional ONNX Runtime. Linux/macOS builds are planned but untested - the CMake falls through to system Qt/OpenCV there.
- **It's a frontend, not a backend.** You need a running ComfyUI instance to point it at (local or remote over HTTP/WS).
- **The facet system needs feeding.** It's the most powerful part and it's the part with the most upfront cost. The triage tooling makes it bearable, not free.
- **Personal project.** Expect rough edges. Feedback and bug reports very welcome.

## Get it

Repo / download / wiki: **[LINK]**

If you try it, I'd especially love feedback on the facet/rules model - that's the part I most want to get right, and it's the part that's hardest to evaluate without other people's tag libraries throwing real edge cases at it.
