# Facet Editor

The surface for assigning **facets** to individual tags. Facets are the metadata that every other part of the app reads to decide how to treat a tag: [[Tag Composer - Rules]] match on them, [[Tag Composer - Groups]] bucket tags by them, the [[Tag Composer]]'s `?` undefined-toggle and quick-add menus flag tags that don't yet have any. The editor is where the mappings actually get created.

Two files back the editor:

| File | What it stores | Edited by |
| --- | --- | --- |
| `data/system/facets.fct` | The **schema**: category blocks (`@category Hair / HairStyle, Bangs, ...`). The canonical list of allowed facet names and how they're grouped for display. | Hand-edited; this page never rewrites it. |
| `data/system/tag_definitions.fct` | **Per-tag mappings** (`red hair = rBody, Hair, Color`). One line per tag. | Rewritten every time you hit Save here. |

> **Screenshot suggestion:** the full page with a tag selected on the left, several pills checked across two categories in the middle, and a Danbooru post image on the right preview rail. Use this as the page header image.

## Layout

Three panels side-by-side:

- **Left (260 px fixed)** - tag list with a filter at the top, plus an optional "UNDEFINED IN COMPOSER" sub-list that appears whenever the composer has tags with no facet definitions.
- **Middle (flex)** - the editor for the selected tag: filter, active-pill strip, scrollable pill checklist grouped by category, save button.
- **Right (380 px fixed)** - Danbooru preview rail showing a representative image for the selected tag.

## Left panel - tag list

### "UNDEFINED IN COMPOSER" section

When the page is shown (or refreshed via `refreshUndefinedList`), the editor asks the composer for its current active tag list and shows any tags that don't have facet definitions in a dedicated sub-list at the top. The header shows the count: `UNDEFINED IN COMPOSER  (7)`.

This is the **fast triage list**. You came here from the composer's `?` badge or its undefined-toggle filter, and you want to clear the backlog: click the first row, assign facets, save - the editor auto-advances to the next undefined tag. When the list drains to zero, the editor emits `composerRequested` and the main window switches you back to the composer page.

If the composer has no undefined tags (or no active tags at all), the section is hidden entirely.

A few mechanics worth knowing:

- **Variable expansion is honored.** `$bgColor$ background` is checked both as the expanded form and as the bare `background` - whichever already has facets is treated as defined. The bare/stripped form is what gets shown if neither has facets.
- **Order matches the composer's tag order**, not alphabetical. You're triaging in the order they were typed/pushed.
- **Selection is one-list-at-a-time.** Clicking a row here clears any selection in the all-tags list below it, and vice versa.

### "TAGS" section (all tags)

Below the undefined sub-list (or directly under the filter when there are no undefined tags) is the full list:

- The union of every tag any entry has tagged AND every tag with a facet definition. Definition-only tags still appear so you can refine them later.
- Sorted alphabetically.
- **Green text** = tag already has at least one facet defined. Default color = no definition.
- A `12 / 207 defined` counter sits between the filter and the list so you can see triage progress at a glance.

### Filter bar

A single-line edit at the top of the panel. Substring-matches against both lists simultaneously (live, no debounce - the lists are small). The clear button (`x`) at the right resets it.

**Press Enter** in the filter to *go to* a tag by its exact name: if the typed string matches an entry in either list, the editor selects it and focuses the first pill in the middle panel. If no exact match exists, the typed string is loaded as a brand-new tag to define (useful when you know a tag should exist but it's not in any entry yet).

### Right-click on a row

Single action: **Go to Wiki**. Routes to the [[Danbooru Wiki]] page with the tag's lookup.

## Middle panel - the editor

Hidden behind a "Select a tag from the list to assign facets" hint until you select one.

> **Screenshot suggestion:** close-up of the editor with two category blocks visible (e.g. "Body" with `Hair` + `Eyes` checked, "Descriptive" with `Color` checked) and the active-pill strip at the top showing all three selected facets.

### Selected-tag header

A single label showing the tag name, tinted by its Danbooru category color (general / artist / copyright / character / meta). Two icon buttons on the right:

- **Open `facets.fct`** in your system editor (for schema edits).
- **Reload schema** - re-reads `facets.fct` only. Tag definitions are untouched. Use this after editing the schema externally.

Right-clicking the label itself opens a "Go to Wiki" action.

### Facet filter

Below the header. Substring-matches the visible pills against the typed query. The match is OR'd against the pill text AND its category name, so:

- Typing `hair` hides every pill whose category isn't "Hair" and whose text doesn't contain "hair" - keeps the whole Hair category visible plus any other pill whose name contains "hair".
- A category whose name matches the filter keeps all of its pills visible (so `body` reveals every facet under the Body category).
- Categories with zero visible pills are hidden entirely.

Empty filter = show everything.

### Active-pill strip

A horizontal flow of "mini" pills representing every currently-checked facet, shown at the top of the editor under the filter. Click a mini-pill to *uncheck* the corresponding facet in the main checklist - lets you remove an active facet without scrolling to find it.

The strip is hidden when nothing is checked.

### Pill checklist (grouped by category)

The bulk of the editor. The schema's `@category` blocks become labeled boxes; each block contains a wrap-flowing grid of checkable pill buttons (one per facet).

- **Click** a pill to toggle it.
- **Arrow keys** navigate between pills (Up/Down/Left/Right walk neighbors visually, not lexically). Tab/Shift-Tab and Space/Enter follow Qt's default focus traversal.
- A pill that's already part of the tag's saved definition starts checked when the tag is selected.

The category blocks themselves preserve the order from `facets.fct`. In your live schema, the **routing-facet categories sit at the top** (`TriggerWord`, `Identity`, `RoutingState`, `RoutingSubject`, `RoutingComposition`) followed by **semantic categories** (`Expression`, `Hair`, `Body`, `Clothing`, ...). Putting routing first makes triage flow correctly: pick the routing facet first, then pick property facets - matches the convention described in [[Tag Composer - Groups]].

### Save button

Bottom of the panel. Text flips between **Save definition** (new tag) and **Update definition** (refining an existing one). What Save does:

1. Collects every checked pill into a facet list.
2. Calls `FacetIndex::setDefinition(tag, facets)`. An empty list clears the tag's definition outright.
3. Rewrites `tag_definitions.fct` to disk (sorted alphabetically for stable diffs).
4. Updates the left list's color/counter for the saved tag.
5. Refreshes the undefined-in-composer list.
6. Emits `facetsDefined` so the composer re-runs its pipeline (the saved tag may now hit a rule or land in a different group).
7. **Auto-advances** to the next tag:
   - If the save came from the undefined sub-list, it advances to the next row in that list, or - when the list is empty - clears the editor and emits `composerRequested` (main window switches back to the composer page).
   - If the save came from the all-tags list, it advances to the next visible row.

That auto-advance is the workflow's productivity multiplier: triaging 20 undefined tags is "select first, assign, Enter Save, assign, Enter Save, ..." until the list drains.

## Right panel - Danbooru preview

Shows one representative image for the selected tag, fetched live from danbooru.donmai.us. The fetch chain:

1. Look up the tag's **wiki page**; if it embeds a `!post #N` reference, fetch that post.
2. Otherwise hit `/posts.json?tags=<tag>&limit=1` and pick the top result.
3. Decode the image and fade it into the rail.

The rail caches by tag so re-clicking the same tag doesn't refetch. Each `fetchPreview` call bumps an internal generation counter; mid-flight chains check it and bail when a newer fetch supersedes them (so rapid-clicking through tags can't end in a stale image fading in).

**Click the preview image** to open the corresponding post page in your default browser. The status label below the image surfaces "Loading...", "No image found", or any network error.

For tags that aren't in the Danbooru index at all (e.g. character names, LoRA trigger words), the rail clears itself and shows no status - the lookup is silently skipped.

## Reloading the schema

Two-button row on the editor header:

- **Open `facets.fct`** - opens the schema file in your default editor.
- **Reload schema** - re-reads `facets.fct` and rebuilds the pill checklist. The tag definitions are **not** touched - if the schema removed a facet that some tags still reference, those tags keep their definition (the orphaned facet just no longer appears as a checkable pill). The [[App Cleanup]] **(stub)** flows handle pruning orphans if you want them gone.

After the reload, the composer's facet index is also refreshed and a status-bar message confirms.

There's no reload button for tag definitions - those are rewritten by every Save here, so the on-disk file always matches the in-memory state.

## Workflow notes

### The composer round-trip

The canonical loop:

1. In the composer, the `?` badge calls out a tag with no facets. Right-click the tag and choose **Edit facets**.
2. The main window switches to this page and selects the tag (it lands in the undefined sub-list automatically).
3. You assign facets, hit Save.
4. The editor advances to the next undefined tag. Repeat.
5. When the undefined list drains to zero, the editor jumps you back to the composer.

You can also drive the same loop by opening this page directly (left nav button) - the undefined sub-list builds itself from whatever the composer currently has active.

### Quick-add facets

The composer's right-click "Quick add as character/copyright/trigger word/style" menu commits one specific facet to a tag without opening this page. The four quick-facet bindings are configured in [[Settings]]. They're the right tool when you know a tag's role without thinking about it (a character name needs `rCharacter` and that's it); this page is the right tool when you need to think.

### Empty save clears

Hitting Save with **zero pills checked** removes the tag's definition entirely (the line drops out of `tag_definitions.fct`). Useful when you've decided a tag shouldn't have facets at all - faster than editing the file by hand.

## Keyboard shortcuts

The editor is keyboard-driven for triage. Bindings vary by where focus currently is.

### Left panel - tag list / filter

Focus is on the tag-list filter edit or on either of the two lists.

| Key | Effect |
| --- | --- |
| Type any printable key (while a list row is focused) | Forwards the keystroke to the filter edit and focuses it. Lets you "just start typing" without grabbing the mouse. |
| `Enter` (in the filter) | Loads the typed string as the selected tag. Exact match in either list selects that row; no match loads a brand-new tag for definition. Auto-focuses the first pill. |
| `Right` (on a list row) | Jumps focus into the middle panel's first visible pill. |
| `Up` / `Down` / `Home` / `End` / `PgUp` / `PgDn` | Standard list navigation. Selection fires `selectTag` so the editor updates as you move. |

### Middle panel - facet filter

Focus is on the "filter facets..." edit.

| Key | Effect |
| --- | --- |
| `Left` / `Right` / `Up` / `Down` (no modifiers) | Jumps focus into the first visible pill. |
| `Shift+Enter` | Clicks the Save button. Same effect as Tab-to-Save + Enter, faster. |

### Middle panel - pill checklist

Focus is on a `FacetPillBtn`.

| Key | Effect |
| --- | --- |
| `Left` / `Right` / `Up` / `Down` | Spatial neighbor navigation - finds the closest pill in that direction, scrolls it into view. Works across category boundaries. |
| `Space` | Toggles the focused pill (Qt default). |
| `Enter` / `Return` | Also toggles the focused pill. |
| `Shift+Enter` / `Shift+Return` | Clicks the Save button. The fastest way to commit while still in the checklist - no need to move focus first. |
| `Escape` | Returns focus to the source list (undefined-in-composer if it's visible and has a selection, otherwise the all-tags list). |
| `Tab` / `Shift+Tab` | Default focus traversal between pills / panels. |
| Type any printable key | Forwards the keystroke to the facet filter and focuses it. Lets you narrow the visible pills without leaving the checklist with the mouse. |

### Right panel - preview image

| Action | Effect |
| --- | --- |
| Left-click the image | Opens the corresponding Danbooru post page in your default browser (no-op if the rail couldn't resolve a post). |

### Fast triage loop (no mouse)

1. Tag list shows the undefined-in-composer rows. Use `Down` to move to the row you want.
2. Press `Right` to jump into the pill checklist (focuses the first visible pill).
3. Use the arrow keys to walk to the pills you want, `Space` or `Enter` to toggle each one.
4. Press `Shift+Enter` to Save. The editor advances to the next undefined tag automatically.
5. Repeat from step 2 until the undefined list drains and the main window switches you back to the composer.

If you need to narrow the visible pills (large schema, unfamiliar facet name), just type while focused on a pill - the keystroke routes to the facet filter automatically. `Esc` returns you to the tag list when you want to pick a different tag.

## Tips

- **Triage from the undefined sub-list, not the all-tags list.** The undefined list auto-advances; the all-tags list also advances but most edits there are *refinements* on existing definitions, not first-time assignments.
- **Mind the routing convention.** A tag should usually have **exactly one** routing facet (`rBody`, `rClothing`, ...) plus any number of leaf facets. The schema groups routing categories at the top so they're the first thing you see - assign the routing facet first, then scroll down to pick properties. See [[Tag Composer - Groups]] for the full convention.
- **Use the facet filter for unfamiliar schemas.** With 100+ facets across 15+ categories, hunting for `Sparkles` by scrolling is slow. Type `spar` in the facet filter.
- **Right-click first to read the wiki.** When a tag's semantics aren't obvious from its name, jump to [[Danbooru Wiki]] via the right-click menu, then come back. The Danbooru preview rail also helps - sometimes the picture answers the question faster.
- **Filter-Enter for new tags.** Type a tag that doesn't exist yet, press Enter - the editor loads it for definition. Saves you from having to push it through the composer first to surface it in the list.
- **`Open facets.fct` for schema edits, in-page editor for definitions.** Schema changes (new category, new facet name) belong in the file; tag-to-facet mappings belong here. Mixing the two means duplicated effort and inconsistent state.

## See also

- [[Tag Composer - Groups]] - how facets get bucketed into composer category headings.
- [[Tag Composer - Rules]] - how `anyTag(facets: ...)` matches against the facets you assign here.
- [[Tag Composer]] - the `?` undefined-toggle and the right-click "Edit facets" jump that lands you on this page.
- [[Danbooru Wiki]] - the in-app wiki view that the right-click "Go to Wiki" actions route to.
- [[Settings]] - quick-facet bindings for the four context-menu shortcuts.
