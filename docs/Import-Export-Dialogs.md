# Import / Export Dialogs

Two modal dialogs accessible from the [[Settings]] page's **DATA** section. They move entries between TagComposer installs (or backup snapshots) as self-contained folders - entries plus the tag definitions those entries actually need.

Both dialogs sit on top of `core::PortManager`, which does the disk work; the dialogs are the surface that lets you pick what gets exported, scan an import folder before committing, and resolve any facet / tag-definition mismatches when the source schema differs from yours.

> **Screenshot suggestion:** the Import dialog with a folder scanned, a non-trivial mapping table visible (some facets matched, some flagged red, some marked Drop / Create), and the validation line at the bottom showing "Will create 2 new facets, will drop 1 facet". Use as the page header image.

## Why a separate import/export at all

A TagComposer install's `data/` folder has a lot of independent pieces (workflows, saved states, settings, the input image cache, ...) and most of them are not portable between machines without thinking. Entries are the one piece that *should* be portable - they're a folder per entry, an image or three, and a flat tag list.

The export/import flow is the path for:

- **Moving a curated set of entries** between two TagComposer installs without copying the whole `data/` tree.
- **Backing up entries you care about** before doing something destructive (purge actions, big tag-schema rewrites).
- **Sharing a tagged dataset** with someone whose facet schema may not match yours - the import dialog's facet-mapping table is the merge surface.

Both flows preserve uuids, so a re-import on the source machine is a no-op (entries already present are skipped as duplicates).

## On-disk layout

The exported folder structure:

```
<destFolder>/
  entries/
    <uuid>/                copied verbatim from data/entry/<uuid>/
      00001.png
      ... etc
  tag_definitions.fct      filtered subset of the source's full definitions
```

`tag_definitions.fct` in the export is **not** the full file - it's a subset filtered to:

- Every tag used by at least one exported entry (always included).
- Optionally, every tag in the source's `tag_definitions.fct` that's *not* in any exported entry but does exist in the source's `danbooru.csv` (i.e. real Danbooru tags, not orphaned typos). This is the "Include unused" checkbox.

The schema file (`facets.fct`) is **not** exported. Sources and destinations can have different schemas, and the import dialog reconciles them via the mapping table rather than overwriting the destination's schema.

## Export dialog

Opened by **Export entries...** in [[Settings]]'s DATA section.

> **Screenshot suggestion:** the Export dialog with a non-trivial query like `kantai collection, -nsfw` typed in, ~30 entries in the preview list, the "Include unused" box checked, and an "Exported successfully to: ..." status line at the bottom.

### Layout

A single column inside a `ChromedDialog` frame (frameless modal with the app's dark chrome). Minimum size 720 x 520.

1. **Prompt** - one-line explainer of the query semantics.
2. **Query line edit** - same grammar as the [[Tile View]] search bar (see [[Search Queries]] for the full reference).
3. **Count label** - `N entr(y|ies) matched`, updated live as you type.
4. **Preview list** - read-only, no-selection list of every matched entry, showing title (or uuid if untitled).
5. **"Include unused" checkbox** - toggles the optional second category in the exported `tag_definitions.fct` (see [On-disk layout](#on-disk-layout)).
6. **Status label** - post-export feedback ("Exported successfully to: ..." or "Export failed: ...").
7. **Buttons** - "Close" + "Export to..." action button.

### Flow

1. Type a query in the line edit.
2. The preview list updates as you type (no debounce - filter is already cheap).
3. **Export to...** opens a folder picker. Pick the destination; the dialog calls `PortManager::exportEntries` to do the actual work.
4. On success, the status line confirms the path. The dialog stays open so you can refine the query and do another export run without closing.

### "Include unused" semantics

When checked, the exported `tag_definitions.fct` also includes every definition from the source's full file that:

- Is **not** referenced by any exported entry's tag list.
- **Is** present in the source's `danbooru.csv` (so it's a real tag name, not an orphan from an old purge).

This is useful for composition-style tags (`cowboy shot`, `from above`, `dutch angle`) - tags you defined facets for once but that aren't sitting in any specific entry yet. Without this checkbox they wouldn't ship with the export.

If the Danbooru index hasn't finished loading at startup, the checkbox is disabled and a tooltip explains why (re-open the dialog after the index loads).

### Errors

The status label surfaces the errors `exportEntries` returned. Common cases:

- Destination folder isn't writable.
- An entry folder is missing on disk (the model has it but the directory was deleted out-of-band).
- An I/O error during the file copy step.

Per-entry copy errors don't abort the whole export - they're accumulated into the error list and the rest of the entries still ship.

## Import dialog

Opened by **Import entries...** in [[Settings]]'s DATA section.

The import dialog is the more substantive of the two - imports are non-trivial because the destination's facet schema is rarely identical to the source's. The dialog is a **scan-then-apply** flow: nothing touches disk until you click **Import**.

> **Screenshot suggestion:** the Import dialog mid-flow with a source folder picked, summary showing some duplicates + collisions, the conflict-mode radios visible, and the mapping table with a mix of green (matched) and red-highlighted (unmatched) rows.

### Layout

Single column, minimum 900 x 720 (mapping tables get tall on a large schema diff).

1. **Source picker** - read-only line edit + Browse button. Pick a previously-exported folder.
2. **Summary line** - count of entries, duplicates, tag definitions, collisions, source facets. Populated after Browse.
3. **Conflict mode** - radio group: Skip / Merge / Overwrite. Decides what happens when the destination already has a definition for a tag the import wants to add.
4. **Facet mapping** section - one row per source facet, with a destination dropdown + Create / Drop toggles. Plus "Create all unmatched" / "Drop all unmatched" bulk buttons.
5. **Validation line** - reports whether every source facet has a decision; the Import button stays disabled until it does.
6. **Status line** - post-import summary.
7. **Buttons** - Cancel + Import.

### Flow

1. **Browse** to a folder previously produced by the Export dialog. The dialog calls `PortManager::scanImport`, which inspects the folder without writing anything.
2. The summary populates: how many entries, how many will be skipped as duplicates, how many tag definitions, how many collide with your current ones, how many distinct source facets.
3. The mapping table fills with one row per source facet. Each row shows:
   - **Source name** (left).
   - **->**
   - **Destination dropdown** - editable combo box pre-populated with your current facets. Defaults to the source name (so a facet already present in your schema auto-matches).
   - **Create button (`+`)** - toggles "make a new facet under `@category Imported`" mode. The dropdown's current text becomes the new facet's name.
   - **Drop button (`x`)** - toggles "strip this facet from any imported tag definition" mode.
4. Source facets that don't exist locally start with the dropdown highlighted **red** (`background: #5a3a3a`) as a "needs a decision" marker. You can't move past validation until every red row has a Drop, Create, or remap action.
5. Pick a conflict mode (default Skip - safest).
6. **Import** runs `PortManager::applyImport` which:
   - Backs up `tag_definitions.fct` to `.bak` first.
   - Copies entry folders into `data/entry/<uuid>/`, skipping duplicates by uuid.
   - Applies facet mapping to imported tag definitions.
   - Resolves tag-name collisions per the conflict mode.
   - Appends any newly-created facets to `facets.fct` under `@category Imported`.
   - Writes the result back.
7. The status line shows the per-category summary (entries imported / skipped; tag definitions added / merged / overwritten / skipped / dropped-empty).
8. Dialog closes.

### Conflict modes (tag-definition collisions)

When the import has a tag definition for `red hair` and your local `tag_definitions.fct` already has `red hair` defined:

| Mode | What happens |
| --- | --- |
| **Skip** | Local definition kept. Import's version is discarded. Counted as a skip in the result summary. |
| **Merge** | Local + import facet sets are unioned. Counted as a merge. |
| **Overwrite** | Local definition replaced with import's. Counted as an overwrite. |

Default is **Skip** so the import is non-destructive unless you opt in. The choice applies uniformly to every tag-collision in this import - per-tag decisions aren't supported through the dialog (and probably wouldn't be ergonomic for an import with hundreds of tags).

### Facet mapping decisions

Each source facet needs one of three decisions:

- **Map to existing** - dropdown points at a facet that exists in your schema. The import rewrites every reference to the source facet into the destination facet. Default state for facets that already exist locally.
- **Create new** (`+` button) - dropdown's text becomes a new facet appended to `facets.fct` under `@category Imported`. The import then references the newly-created facet. Use when the source has a facet you'd like to adopt as-is.
- **Drop** (`x` button) - the facet is stripped from every imported tag definition. If a definition's *only* facets were dropped, the whole definition is dropped (counted under "tagsDroppedEmpty" in the result summary).

`+` and `x` are mutually exclusive on a single row (clicking one un-clicks the other).

### Bulk decisions

Two buttons at the top of the mapping section apply a default to every still-unresolved row:

- **Create all unmatched** - sets `+` on every row whose dropdown text isn't in your schema. Useful when you want to wholesale-adopt the source's schema.
- **Drop all unmatched** - sets `x` on the same set. Useful when you want to keep your schema clean and discard source-only facets entirely.

Rows that already match an existing facet aren't touched by either bulk action.

### Validation

The Import button stays disabled while any source facet has no decision. The validation line above the buttons spells out what's missing: `N facet(s) need a decision (Drop, Create, or map to existing): facet1, facet2, ...`.

Once every row has a decision, the line flips to a summary:

```
Will create 2 new facet(s) under @category Imported: Foo, Bar
Will drop 1 facet(s) from imported defs: Baz
```

That's your last preview before pressing Import.

### Result summary

After Import runs, the status label shows:

```
Entries imported: N · skipped (duplicates): M
Tag definitions - added: X · merged: Y · overwritten: Z · skipped: W · dropped (empty after mapping): V
```

If the operation hit any errors, they're appended on the next line: `Errors: <error1>; <error2>; ...`.

The dialog closes after Import even if there were errors - the status line is your record. Check the [[Settings]] log section for the persistent copy.

## Round-trip semantics

A clean round-trip (export from machine A, import on machine B, export from B, import on A) preserves entries faithfully:

- Entries are keyed by uuid; re-importing an entry already present is a no-op.
- Tag definitions are stable under Merge mode if the schemas match.
- Schemas don't ship, so the first import from a foreign source requires the mapping step; subsequent imports between the same two installs go through it cleanly because the facets are already aligned (or you've adopted them via Create).

The `.bak` backup of `tag_definitions.fct` is your safety net - if an import does something unexpected, swap the `.bak` back in before the next clean shutdown rewrites the file.

## Tips

- **Export refines as you type.** No need to click anything between query edits - the preview list updates live. Refine until the matched count looks right before clicking "Export to...".
- **Use "Include unused" when sharing a facet schema.** Without it, only tags that some entry actually uses ship with the export. The receiver's `tag_definitions.fct` will be sparser than yours, which may bite later.
- **Drop > Create when in doubt.** A dropped facet stays out of your `facets.fct` and out of every imported tag definition. Create permanently adds a new facet to your schema; you'd then have to live with (or hand-edit out) the new `@category Imported` block.
- **Skip is the safe conflict mode.** It's the default. Pick it whenever you want imports to be additive-only and never modify your existing tag definitions.
- **Bulk buttons short-circuit triage.** If you're importing a hundred entries from a foreign schema and you know you'll Drop the lot, hit "Drop all unmatched" once and move on.
- **The `.bak` is your undo.** Tag-definition imports are not undo-able from inside the app, but `data/system/tag_definitions.fct.bak` is written before every import. Copy it back over the live file (with the app closed) to roll back.
- **Schemas don't sync automatically.** Adopting a new facet via Create writes to `facets.fct`. Restart the app to pick up the schema change in the [[Facet Editor]] pill grid.

## See also

- [[Settings]] - DATA section hosts the buttons that open these dialogs.
- [[Search Queries]] - the query grammar driving the Export dialog's entry selection.
- [[Facet Editor]] - where the imported facets eventually get used; Create-via-import adds rows here.
- [[Tile View]] - what the imported entries land in.
