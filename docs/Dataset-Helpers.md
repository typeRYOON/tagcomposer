# Dataset Helpers

A five-tab page for the dataset side of the workflow: building image collections, auto-tagging them, hand-tuning the resulting tag sidecars, and bulk-editing across the set. None of this touches the [[Tile View]]'s entry model directly - dataset helpers work on plain folders of `<image>` + `<image>.txt` sidecar pairs, the same shape AI training pipelines (kohya / sd-scripts / similar) consume.

> **Screenshot suggestion:** the page with the tab bar visible at top showing five buttons, the animated pill indicator under "Auto-tagger", and the auto-tagger's three-column layout below. Use as the page header image.

## The five tabs

The tab bar runs across the top of the page (50 px tall, dark fill + bottom rule). The active tab is highlighted by a pill indicator that slides between buttons - the animation is purely cosmetic, the underlying widget swap is instant.

| Tab | Purpose |
| --- | --- |
| **Tag Cluster** | Mines Danbooru's `/posts.json` for tags that co-occur with a seed tag. Ranks them by PMI and surfaces a triage list you can copy or turn into a new tile-view entry. |
| **Auto-collect** | Watches a downloads folder, dedupes via perceptual hash, and routes new images into a named collection folder under `data/collections/<name>/`. |
| **Auto-tagger** | Runs an ONNX classifier (a wd14-style tagger) over a folder of images, writing one `.txt` sidecar per image with the predicted tags. Threshold + cooldown configurable. |
| **Tag Editor** | Hand-tune sidecars one at a time. Image preview + tag textarea + multi-color search highlight + danbooru-autocomplete add bar + navigation. |
| **Batch Edit** | Bulk in-place edits across every sidecar in a folder: remove tag, remove first, prepend, append. Optionally log tag frequencies. No undo - operates on source files. |

Tabs sit in the order of the typical workflow (collect -> tag -> edit -> batch). Tag Cluster lives at the front because it's the exploratory step you do *before* committing to a dataset shape.

## Inter-tab handoffs

Four of the tabs can hand their output folder to the next stage with one click. The handoff sets the target tab's input field and switches the tab bar in one shot.

| From | To | Button |
| --- | --- | --- |
| Auto-tagger | Tag Editor | "Send to Tag Editor" |
| Auto-tagger | Batch Edit | "Send to Batch Edit" |
| Tag Editor | Batch Edit | "Send to Batch Edit" |
| Auto-collect | Auto-tagger | "Send to Auto-tagger" |

The handoff is just folder routing - it doesn't trigger the target's run. You still click Run / Save / whatever to actually execute on the target page.

## Tag Cluster

Mines a Danbooru-style booru for tag co-occurrence. Given a seed tag (typically a character name or copyright), it fetches a sample of that seed's posts, counts which of them carry each other tag, and ranks them by **PMI (pointwise mutual information)** against the corpus-wide tag frequencies baked into the bundled `danbooru.csv`.

> **Screenshot suggestion:** the tab with a seed tag like `hatsune miku` mined, the PMI slider set mid-range, ~30 result rows on the right, and the in-tag-cluster preview image showing on the far right.

### Left panel - parameters

- **Tag input** - the seed tag (Danbooru-style: underscores; backslash-escaped parens from a composer prompt are accepted too).
- **`+solo`** - if checked, the character query is restricted to `solo` posts (cleaner co-occurrence). Characters with few solo posts return less data, so leave it off if results are sparse.
- **`single character tag only`** - drops fetched posts that list more than one character tag, keeping alt-form / skin variants from muddying the cluster.
- **`Char pages`** - how many pages of the seed's posts to fetch (200 posts per page). More pages = stronger PMI signal, slower fetch.
- **Min % slider** - drop tags that appear in fewer than this *share* of the fetched posts, before PMI ranking. It resolves against the actual fetch size, so it scales with `Char pages` automatically (1% of a ~3000-post sample is ~30 posts; 1% of a ~200-post niche character is ~2). Bump it up for cleaner results on a big character; drop it for a sparse one. Live-applied.
- **Min PMI slider** - threshold below which tags are hidden. Live-applied, no re-fetch.
- **Fetch** button - pulls the seed's posts from Danbooru and builds the cluster.
- **Cancel** button - enabled only while a fetch is running. Stops it mid-stream; whatever pages were already pulled are kept and scored (the status bar reports how many posts / tags that was), so you can ballpark a cluster without waiting for all the pages.

While fetching, the status line shows a running tally (`Fetching... N posts, M tags so far.`) and the progress bar tracks pages. When it finishes - or you cancel - the status settles to `N posts, M tags. Showing K ...` (and, if `single character tag only` dropped any, `... (J multi-character skipped) ...`), so you always know how big the sample behind the cluster actually was.

The PMI baseline (per-tag post counts) comes from `danbooru.csv` - the same file that drives autocomplete - so there's no separate "global" fetch or cache to manage. Tags absent from that file, or below a small post-count floor, are dropped from the results.

### Filter editor (left panel, below)

A blacklist / whitelist for tags you never want to see in cluster results regardless of PMI - things like `signature`, `original`, `commentary_request`, and other meta noise. Stored at `data/system/cluster_filters.fct`.

- **Blacklist / Whitelist radio** - which mode.
- **Tag list textarea** - one tag per line.
- **Save** - persists to disk.

### Results (right side)

- **Result rows** - one per surviving tag, sorted by PMI descending. Each row shows the tag name, its PMI score, and an `x` to dismiss (excludes from the copy buffer + the "Create entry" tag list). Right-click for the standard Wiki / Edit facets / Quick-add menu.
- **Image preview rail** - hovering a row fetches a representative Danbooru post image (cached) and shows it on the right. Click-through to the post page.
- **Copy edit + Copy button** - the live comma-joined list of included tags. Edit it in place and Copy puts the result on the clipboard.
- **Create entry** - calls back into the [[Tile View]] to mint a new entry seeded with the cluster tags. Useful when the cluster you just mined is the start of a new character / copyright entry.

### What's PMI?

`PMI(t | seed) = ln( P(t among the seed's posts) / P(t across all of Danbooru) )`. The numerator is how often the tag showed up in the fetched sample; the denominator is the tag's `danbooru.csv` post count divided by the corpus size. High PMI = the tag is meaningfully enriched for this seed (more than chance); near-zero = it's just popular everywhere (low score even if it co-occurs a lot). The slider tunes how enriched a tag has to be to surface; the score column shows the raw PMI so it lines up directly with the threshold.

## Auto-collect

A "downloader hopper" tab. Point it at the folder your browser dumps `.png` / `.jpg` into; when a new image lands, it gets `pHash`'d, deduped against the active collection's index, and either:

- **Moved** into `data/collections/<collection>/` if it's novel (Hamming distance above the threshold against every previous hash).
- **Sent to recycle bin** if it's a near-duplicate of something already collected.

> **Screenshot suggestion:** the tab with the watcher running (green dot), 12 images collected in the right thumb strip, and a few skip lines in the activity log.

### Left panel - controls

- **Watch folder** - the downloads directory to monitor. Browse-button next to it.
- **Collection dropdown** - the active collection. New ones can be created via the `+` button (prompts for a name; creates `data/collections/<name>/`).
- **Threshold slider** - 0..64 Hamming distance. Higher = more permissive (closer matches still admitted as "novel"). 0 = exact-bit-match dedup; 8-12 is typical for "skip near-duplicates of the same image".
- **Poll interval spinbox** - seconds between scans. Lower = snappier; higher = less file-system pressure.
- **Start / Stop** - toggles the watcher.
- **Open folder** - opens the active collection in the OS file explorer.
- **Rebuild index** - rescans the collection and re-hashes every file. Use after manual edits to the folder (or when threshold-tuning produces inconsistent dedup decisions).
- **Send to Auto-tagger** - handoff (see [Inter-tab handoffs](#inter-tab-handoffs)).

### Middle panel - activity

- **Active dot** - green when the watcher is running, grey when idle.
- **Counters** - "Collected: N, Skipped: M" for the session.
- **Log** - timestamped lines for each decision (moved, skipped-duplicate, error). Read-only.

### Right panel - recent thumbs

A capped grid of thumbnails of the most-recently-collected images, newest first. Oldest thumbs fall off the bottom as new ones arrive. Decode is async per-thumb (worker pool).

## Auto-tagger

Runs an ONNX classifier (typically a wd14-style tagger or any compatible model) over a folder of images and writes one `.txt` sidecar per image. The sidecar is a comma-separated list of tags whose predicted confidence cleared the threshold, with the rating tag (`general` / `sensitive` / `questionable` / `explicit`) prepended.

> **Screenshot suggestion:** the tab with a run in progress (progress bar visible), a half-populated results list, and the right-pane focused result showing an image plus its predicted tags.

### Left panel - parameters

- **Model dropdown** - lists every model installed under `data/models/`. To add a new tagger, drop its ONNX folder into `data/models/<model-name>/` (typically containing `model.onnx` and a `selected_tags.csv`) and the dropdown picks it up on next page show. Hit **Refresh models** (or just restart) if you added a folder while the page was open.
- **Input folder** - the images to tag. Browse-button.
- **Output folder** - where the `.txt` sidecars (and optionally the moved images) land. Leave empty to write sidecars next to the source images.
- **Threshold slider** - 0.0 to 1.0. A tag is included if its predicted confidence is above this value.
- **Cooldown spinbox** - milliseconds between images. Use a non-zero value when running on a thermally-constrained machine or sharing the GPU with another process.
- **Recursive checkbox** - scan subfolders.
- **Move checkbox** - move (rather than copy) the input image into the output folder alongside its sidecar.
- **Run / Cancel** - start / stop the batch. Inference runs on a worker thread (`core::BatchTagger`); the GUI thread stays responsive.
- **Send to Tag Editor / Send to Batch Edit** - handoffs.

### Middle panel - results list

One row per tagged image (with its filename). Click a row to focus its results on the right. Failed images show a row with the failure reason.

### Right panel - focused result

- **Image preview** - the source image, clickable to open in the OS viewer.
- **Rating label** - the predicted rating (e.g. "general (0.94)").
- **Tag list** - the tags written to the sidecar, with confidence scores.

Cached per-image, so clicking through old rows brings back the previous result without re-running inference.

## Tag Editor

Hand-tune the `.txt` sidecars produced by Auto-tagger (or any other source). Pure file I/O - this page never invokes a model.

> **Screenshot suggestion:** the tab with a folder of ~50 images, the middle pane showing the large preview, and the right pane's tag textarea highlighting "red, hair" matches in two different colors.

### Left column - folder + navigation

- **Folder input** + Browse - the directory to scan.
- **Recursive** checkbox.
- **Position label** - `47 / 213` style.
- **Navigation buttons** - first / -10 / prev / next / +10 / last for moving through images alphabetically.
- **Delete** - removes the current image and its sidecar from disk. No confirmation; the file goes to the recycle bin.
- **Send to Batch Edit** - handoff.

### Middle column - preview

A large image label that scales the source pixmap to fit the column. The image name shows in the IMAGE section header.

### Right column - editor

- **Tag search bar** - Danbooru-style autocomplete. Picking a suggestion adds it to the editor (comma-joined, canonical underscore form).
- **Tag textarea** - the sidecar contents. Edits are debounced and auto-saved (no save button).
- **Highlight bar** - comma-separated patterns to highlight in the editor. Each pattern gets its own color from a small palette so multiple searches are visually distinguishable. Useful for triaging "find every image where I forgot to add `solo`".
- **Status label** - "Saved" / "Saving..." / "Error: ..." feedback.

The active-tags set (parsed from the editor on each edit) is fed to the autocomplete bar so it can flag attempts to re-add a tag the file already has.

## Batch Edit

Apply the same set of operations to every `.txt` sidecar in a folder. Operations apply in a **fixed order** so the result is predictable regardless of which checkboxes you toggle.

> **Screenshot suggestion:** the tab with all four operations checked, a successful run completed with the progress bar full and a multi-line log showing files written.

### Operations (in order)

1. **Remove tag** (specific tag, input field) - strips every occurrence of the named tag from every sidecar.
2. **Remove first** - drops the first tag from each sidecar. Useful when Auto-tagger prepended a rating you no longer want.
3. **Prepend** (input field) - adds the input text at the start of each sidecar (with a trailing comma).
4. **Append** (input field) - adds the input text at the end of each sidecar (with a leading comma).
5. **Log tag frequencies** - read-only. Walks every sidecar, counts every tag, writes a sorted frequency table to the log pane. Doesn't modify any file.

Each operation has its own checkbox; only the checked ones run.

### Controls

- **Folder input** + Browse + Recursive.
- **Run** - executes the operations in order across every `.txt` in the folder.

### Right panel - output

- **Status label** + **progress bar**.
- **Log textarea** - per-file lines (`<filename> - <op>: before -> after`).

**No undo.** The page rewrites the source `.txt` files in place. Snapshot the folder externally if you might want to roll back.

## The typical workflow

The five tabs were ordered to support a left-to-right pipeline, though every step is independent:

```
1. Tag Cluster      Explore: "what tags actually co-occur with this character?"
2. Auto-collect     Hopper: route a Danbooru / Pixiv save spree into a clean dedup'd folder.
3. Auto-tagger      Inference: write predicted tags as .txt sidecars.
4. Tag Editor       Triage: walk through and fix the predictions image-by-image.
5. Batch Edit       Bulk: strip the prepended rating, prepend a trigger word, etc.
```

The handoff buttons stitch (2) -> (3) -> (4) and (4) -> (5) without ever leaving the page.

## Tips

- **Tag Cluster needs internet.** It hits Danbooru's public API. If you're on a captive network or the booru is down, the fetch will fail with a status message.
- **Cluster filter is per-app, not per-seed.** The blacklist / whitelist applies to every cluster fetch. Use it for meta noise (`signature`, `commentary_request`, etc.) you never want surfaced.
- **Auto-collect's threshold is a Hamming distance, not a percentage.** Larger numbers = more lenient. 0 = byte-for-byte image dedup; 64 = "everything's a dupe" (never collects).
- **Auto-tagger's cooldown helps on shared GPUs.** A 100-500 ms cooldown noticeably reduces VRAM pressure when ComfyUI is running on the same card.
- **Tag Editor auto-saves.** Don't look for a Save button. Watch the status label - it flips to "Saved" once the debounce timer flushes.
- **Tag Editor's highlight is multi-color.** Comma-separate patterns to give each one its own color: `red, blue, ahoge` paints three distinct highlights, making it easy to spot which images have which combos.
- **Batch Edit has no undo.** Always test on a single-image folder first when trying out a new op combination. Or zip the folder before running on the real dataset.
- **The handoff buttons don't run anything.** Going `Auto-tagger -> Tag Editor` brings you to the editor with the folder pre-filled, but you still arrow through images yourself.
- **Sidecars live next to their images.** The Tag Editor and Batch Edit operate on `<folder>/<basename>.txt` for every image - same convention kohya / sd-scripts / similar trainers expect, so the folder is training-ready as-is.

## See also

- [[Tile View]] - destination of Tag Cluster's "Create entry" action.
- [[Facet Editor]] - destination of the right-click "Edit facets" action available on Tag Cluster result rows.
- [[Danbooru Wiki]] - destination of the right-click "Wiki" action.
- [[Settings]] - holds the per-tab persistent state (folders, threshold, model selection, etc.).
