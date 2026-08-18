# Tag Composer

The prompt-building surface. Tags from pushed entries (plus anything you've added by hand) flow through a pipeline of facet-aware rules and replacement variables; the result is a final positive prompt that's queued against ComfyUI through a selected workflow JSON template.

> **Screenshot suggestion:** the whole composer page after pushing a couple of entries - sidebar on the right populated with rules / workflow list / variables, center showing several category groups of tags, floating preview / control bar visible bottom-right. Use this as the page header image.

## What it produces

Hit **Run** and the composer:

1. Walks the active tag list through the [pipeline](#the-pipeline) (rules, replacement vars, facet lookups).
2. Joins the surviving, ordered tags into the positive-prompt string (with weights baked in if any non-1.0).
3. Loads the selected workflow JSON template and substitutes its variables (`__seed__`, `__steps__`, ..., plus the built-in `__positive__`).
4. Validates the result (see [[ComfyUI Integration]] for the run-block rules).
5. Posts the JSON to ComfyUI and logs the push into [[Prompt History]].

Re-runs (count > 1, or wildcards) loop steps 1-5 with fresh wildcard picks and a fresh seed advance per iteration.

## Layout

Three columns, plus a few floating elements anchored to the page corners:

- **Center column** - search bar at the top, scrolling list of category groups below it.
- **Sidebar (right, 280 px wide)** - three stacked sections: **Rules**, **WF / States** (tabbed), **Variables**.
- **Floating overlays** - category nav panel and Clear button at top-right; preview image and control bar at bottom-right.

The center column has no left padding so category headers sit flush against the nav rail.

> **Screenshot suggestion:** annotate the three regions on a full-page capture - center, sidebar, floating overlays.

## Center column

### Search bar (tag input)

The same `TagSearchBar` widget used in the entry panel: a single line edit with Danbooru-style autocomplete. Type a tag, press Enter / Tab / click a suggestion - the tag is added to the active set and the page rebuilds.

- **Typing without Enter** acts as a *filter* on the visible tag rows (debounced 180 ms). The filter is case-insensitive substring matching against the tag string in each row. Clearing the bar restores the full view.
- **Pasting** comma-separated text adds each piece as a distinct tag.
- **Down arrow** with empty input jumps focus into the groups list, selecting the first row.
- **Escape** returns focus from the bar back to the groups (or the page).

### Tag groups

Below the search bar, tags are bucketed into ordered category groups - the categories themselves and their order come from `groups.fct` (see [[Tag Composer - Groups]] **(stub)**). A typical group might be *Subject* > *Character* > *Copyright* > *Style* > *Quality*, but it's user-configurable.

Each category has:

- A **section header** with the category display name (clicking it from the nav panel scrolls here).
- A list of **tag rows** in the order the pipeline emitted them.

A special **Deactivated** group appears at the bottom whenever you've deactivated any tags - they stay listed (strike-through) so you can reactivate without retyping.

> **Screenshot suggestion:** one category fully expanded with at least one row of each result kind (Include, Injected, Skipped/Replaced, Flagged, NoFacets, Deactivated) so the dot colors and badges show.

### Custom tags (free text in a group)

Right-click a **group header** for *Add tag to \<group\>...*, or right-click blank space in the groups area for an *Add tag to* submenu listing every group (that's how you reach a group with no rows yet). The prompt takes arbitrary text - a whole natural-language sentence is fine, which is what Anima-style models want.

The injected text is stored as an ordinary entry in the active tag list, and the app assigns it **the facets that group requires**, so it qualifies for the group without a `tag_definitions.fct` entry. From there it is a normal tag: rules match it (`anyTag(facets: ...)` sees the injected facets), `$vars$` inside it expand, weights apply, group ordering and [[Tag Composer]] profiles position it, and format profiles wrap it if the group's facet is in the active format list.

Where the facets live matters: they are **composer state**, saved into `session.json`, into each saved state, and into the baked PNG stamp - *not* into `tag_definitions.fct`. A one-off sentence has no business in the global vocabulary, and Settings' "Purge tag definitions" drops anything that isn't in the Danbooru list and isn't used by an entry, which is exactly what these look like.

Notes:

- The facets are a **snapshot** of the group's facet list at injection time. Editing that group in `groups.fct` afterwards doesn't retarget existing custom tags.
- Group matching is first-match-wins, so if a broader group sits above the one you picked, it claims the facets first - the status bar says so instead of silently misfiling the text.
- Renaming the row (inline edit) carries the facets across; deleting it, clearing the composer, or restoring another state drops them.
- Text with commas stays one entry when injected this way; the search bar still splits on commas, so use the group menu for prose.

### Tag row anatomy

Each row carries up to six visual elements; only the ones that apply are drawn:

| Element | Meaning |
| --- | --- |
| Colored dot (left) | The `RuleResult` for this tag: muted-green = Include, bright green = Injected by a rule, dark = Skipped / Deactivated / (Deleted), red = Replaced by a rule, amber = Flagged, blue = NoFacets. |
| Tag name | The tag string. Editable inline for plain Include/NoFacets/Flagged rows; read-only for Injected/Skipped/Replaced (Skipped/Replaced/Deactivated rows are also strike-through). |
| `$VAR$` badge | Yellow-ish chip when the tag came from a replacement variable. Shows the source `$name$`. |
| `?` badge | Blue chip if the tag has no facets defined in `tag_definitions.fct`. |
| `[flag]` badge | Amber chip for a rule that Flag-marked the tag. The flag label comes from the rule. |
| `↑ ruleSource` badge | Shown for Injected rows, names the rule that introduced the tag. |
| Weight spin box | 0.10 - 5.00 in 0.05 steps. Non-1.0 weights tint the box. |
| `×` delete button | Removes the tag from the active set. Not shown for Injected rows (those come from a rule, edit the rule to drop them). |

Right-clicking a row opens a context menu: **Wiki**, **Edit facets**, **Deactivate / Activate**, **Remove**, optionally **Change variable** (when the row is a `$VAR$` expansion - lets you swap or drop the source var), and the four **Quick add as character/copyright/trigger word/style** entries that funnel straight into the facet editor for triage.

### Keyboard navigation inside the groups

When focus is on the groups area:

- **Up / Down** moves the selected row.
- **Delete** removes the selected row's tag.
- Typing any printable character (or **Backspace**) focuses the row's inline editor and replays the keystroke, so you can rename a tag without grabbing the mouse.
- **Tab** from inside the list jumps to the search bar.

## Sidebar

> **Screenshot suggestion:** sidebar in three states - (a) WF tab active with a workflow selected, (b) STATES tab active with a couple of saved states (one with a preview thumbnail), (c) the variables row at the bottom with one variable defined.

### Profiles section

Two combo boxes at the top of the sidebar, pinned above the resizable sections:

- **Groups** - the active *group profile*: a named ordering of the groups already defined in `groups.fct`. It never redefines a group's name or facets, it only permutes them.
- **Format** - the active *format profile*: a named set of per-facet tag wraps (the same `prefix + tag + suffix` mechanism as Settings' "Tag formatting"). This is what adds or removes the leading `@` on artist tags.

The two are **independent axes** - any ordering pairs with any formatting - which is the point: switching between model conventions (e.g. Illustrious vs Anima) usually means changing one of them and not the other.

Both live in `data/system/profiles.fct`. Header buttons: **open `profiles.fct`** in your editor, **reload** from file. There is no in-app editor; the file is the editor, same as `rules.fct`.

```
@groupprofile Anima
    Style, Body/Hair, Body/Eyes, Body, Clothing
@formatprofile Anima
    rStyle = @ |

active = Anima | Anima
```

Notes:

- A group profile may name a **subset**. Groups it doesn't mention keep their `groups.fct` order and follow the named ones, so adding a group doesn't invalidate every profile.
- A format line is `facet = prefix | suffix`; either side may be empty. Quote a side to keep leading/trailing spaces (`rStyle = "@ " | ""`).
- Unknown group names are ignored on load (the group may have been renamed).
- The active pair lives in `profiles.fct`, not `session.json`, so it survives a session reset.
- Reordering changes **which group claims a tag**, not just where it lands in the prompt: groups match top-to-bottom, first match wins. Putting a broad group (`rBody`) above a narrow one (`rBody, Hair`) makes the narrow one unreachable - the status bar warns with `X shadows Y` when a switch produces that.
- With no format profile active the combo shows `(settings.json)` and Settings' "Tag formatting" list is used - the legacy source still works.

Saved states record both the **name** and the **resolved snapshot** of each profile, so a state saved under "Anima" replays with the `@` even after that profile is edited or deleted. When the snapshot no longer matches any profile the combo shows `Anima (from state)`. Because the snapshot rides in `SavedState`, it also travels inside baked-state PNGs.

### Rules section

Lists every rule loaded from `rules.fct`. Each row has:

- A checkbox - enable / disable the rule.
- The rule **name** (elided to fit).
- A **⚑** badge if the rule is marked "force" (always evaluated even if disabled - mostly for safety nets).
- A **✚** badge for *Add* rules, **⇄** badge for *Replace* rules.
- If the rule is *Add* or *Replace*, an indented list of its tag arguments, each with a `✕` to remove and a trailing `add tag…` line edit to append. Edits commit on focus loss; persist to `rules.fct` immediately.

Edit-in-place lets you tune a rule's arguments without leaving the composer. The full rule grammar (match expressions, action types) lives on [[Tag Composer - Rules]] **(stub)**.

Header buttons: **open `rules.fct`** in your editor, **reload** from file.

### Workflow / States tab

A two-tab area: **WF** (workflow list) and **STATES** (saved snapshots). Headers swap a small icon depending on which tab is active.

**WF tab:**

- A filter line edit (substring on workflow name).
- A list of registered workflow JSONs. Click to select - the selected workflow becomes the target for the next Run.
- **Drop `.json` files** onto the list to add them. The file is copied into `data/workflows/` (so the data folder stays portable) and registered with a `name` taken from the filename.
- Right-click a workflow for **Open file**, **Rename**, **Remove**.
- The **open-external** icon in the header opens the [[Workflow Editor]] page for the selected workflow.

**STATES tab:**

- A filter line edit (substring on state name).
- A list of saved composer states, newest first. Click to **restore** - replaces the composer's active tags, weights, rule enablement, replacement vars, workflow selection, workflow vars, and active LoRA stack with the snapshot's values.
- **Drop an image** onto a row to attach it as that state's preview thumbnail (resized to `data/states/<id>/preview.png`).
- Hovering a row with a preview pops up a floating preview window.
- Right-click for **Open state file**, **Overwrite with current**, **Rename**, **Delete**.
- The **+** icon in the header saves the current composer state as a new entry (prompts for a name).

### Variables section (composer replacement vars)

These are the `$name$` substitutions that get expanded in tag strings *before* the pipeline runs (e.g. a tag like `hair color: $hairColor$` gets the live value). One row per variable:

- `$name$` label.
- Value editor (line edit). Commits on focus loss.
- `✕` to remove.

A trailing add-row at the bottom takes a name + value pair (Enter commits). Duplicate names clear and refocus the input as a soft "rejected" hint.

> Note: these are **composer-level** replacement vars, not the same thing as **workflow** vars (`__seed__`, etc.) - workflow vars are managed in the [[Workflow Editor]].

## Floating overlays

### Category nav panel (top-right)

A small vertical handle with the active category names. Hover to expand the list; click a name to smoothly scroll the groups view to that category's header. Disappears when the composer is empty.

### Clear button (top-right, left of the nav)

A `✕ Clear` button. Drops everything composer-side: disables all rules (writes back to `rules.fct`), empties the active tag set + weights + deactivated set, drops all entry pushes (emits `activeGroupsChanged({})` so the tile view's green borders clear), and deactivates all LoRAs.

Workflow selection and saved states are **not** touched.

### Preview image (bottom-right)

A 200 x 200 thumbnail that fades in when the first generated image arrives from ComfyUI. Click to open a frameless popout window that follows the workspace and mirrors the composer's progress + active count. Closing the popout restores the inline preview.

> **Screenshot suggestion:** the popout window detached, sitting next to a different monitor / pane, showing a generated image and the mirrored status bar.

### Control bar (below the preview)

Six controls in a row:

- **`?` Undefined toggle** - filters the tag rows to show only those without facets defined. Same triage hook as the `?` badge.
- **Copy prompt** - puts the current positive prompt string on the clipboard. Uses the human-readable form (no JSON escaping).
- **`↶` Undo** / **`↷` Redo** - 50-deep stack of composer snapshots. Coalesces same-kind edits within ~500 ms so slider drags / typing don't fill the stack. Shortcuts: Ctrl+Z / Ctrl+Shift+Z.
- **Run** - triggers a queue against ComfyUI. The adjacent number spinner sets the iteration count (1-99).
- **`■` Interrupt** - cancels the in-flight job. Shift-click interrupts and clears the pending queue.

> **Screenshot suggestion:** close-up of the control bar with the count spinner set to something memorable (e.g. 4) and the undefined-toggle pressed so the highlight state is captured.

## The pipeline

Every change that affects the prompt (push, manual add, rule toggle, var edit, weight tweak, deactivation, ...) calls `repush()` on the next event-loop tick. `repush()` collects the live tag set (excluding deactivated entries), hands it to `PromptPipeline::push()`, and waits for the `pipelineReady` signal.

The pipeline:

1. **Expands `$var$` references** in each tag against the composer's variable index.
2. **Looks up facets** for each tag in `tag_definitions.fct`. Tags without facets get `RuleResult::NoFacets` (and the blue `?` badge).
3. **Runs enabled rules** against the resulting facet sets. Each rule can:
   - **Include** (keep the tag, default).
   - **Add** new tags (`RuleResult::Injected`).
   - **Replace** the tag with new ones (`RuleResult::Replaced` for the original, `Injected` for the replacements).
   - **Skip** the tag from the output (`RuleResult::Skipped`).
   - **Flag** the tag with a label (still included, but marked).
   - **Delete** the tag entirely (drops from the active set, not just from output).
4. **Buckets** the surviving tags into category groups per `groups.fct`.
5. **Emits** the categorized list back to the composer for display.

The composer applies per-tag weight overrides last, so weights aren't lost across pipeline runs.

### Coalescing bursts of tag additions

The pipeline is expensive enough that running it once per added tag would visibly lag the UI in one specific case: **comma-separated paste**. The tile view's entry-panel tag search bar (and the composer's own search bar) split a pasted/typed `taga,tagb,tagc` on commas and emit one signal per tag - the entry panel issues `entryTagAdded` three times, the composer's `onEntryTagAdded` slot fires three times, and without coalescing the pipeline would run three times in the same event-loop tick.

`queueRepush()` solves this. The first caller sets `m_repushPending = true` and posts a `repush()` invocation via `Qt::QueuedConnection`; subsequent callers in the same tick see the flag and bail out. By the time `repush()` actually runs, `m_activeTags` already contains all three tags, so a single pipeline pass produces the final result. Same trick covers any other "fire once per N items" caller (rule arg edits that touch several args, state restores that walk every entry push, etc.).

Direct edits that don't burst - a slider drag, a single tag rename - still get coalesced if they happen to land in the same event-loop tick, but mostly they each fire one pipeline run.

## Active pushes (entries)

When a tile-view entry is "pushed" (composer toggle on its tile, or the right-click Add-to-Composer action), the entry's tag list is unioned into the composer's active tags. The composer tracks ownership in `m_activePushes` keyed by `(entryId, imageIdx)`. Un-pushing the entry removes only the tags that no other push (or manual add) is claiming - so partial overlaps survive correctly.

Rename / delete / image-remove events on the entry model bubble through to keep the push state coherent: a deleted entry drops its tags and LoRA from the composer; a removed image slot reindexes the higher slots; renaming a tag (in the entry panel or here) rewrites the push record so the next un-push removes the new name.

## Wildcards (per-run randomization)

A workflow can declare *wildcard* variables (one-line-per-slot tag lists; see [[Workflow Variables]]). On Run, the composer calls `pickWildcardTags()` to choose one entry from each wildcard list, then folds those tags into the positive prompt via `computePromptWithExtraTags`. Wildcards do **not** appear in the composer's active set - they're transient per iteration.

When iterating (`count > 1`), each iteration re-picks wildcards, so the same Run can dispatch multiple distinct prompts.

## Workflow vars at run time

The selected workflow's variables (placeholders like `__seed__`, `__steps__`, `__neg__`) are substituted by `WorkflowManager::applyToJson` at Run time. Built-ins handled automatically:

- `__positive__` - the rendered positive prompt (with JSON escaping).
- `__lora_count__`, `__lora_name_<N>__`, `__lora_wt_<N>__`, `__lora_model_str_<N>__`, `__lora_clip_str_<N>__` (N = 1..10) - the active LoRA stack.

Anything else needs a variable declared on the workflow - configured in the [[Workflow Editor]]. Run validation (see [[ComfyUI Integration]]) blocks the push if any declared var is unused, any unresolved `__dunder__` survives the substitution, or any active LoRA lacks a slot in the template.

## Sessions and states

- **Session save/restore** is automatic on app shutdown / startup: composer state, workflow selection, LoRA stack, and the active push set survive a restart. The file is `data/system/session.json`.
- **Named states** (saved via the `+` button in the STATES tab, or by **Save as state** from [[Prompt History]]) are stored as `data/states/<id>/state.json` plus an optional `preview.png`. Restoring a state replaces the live composer state wholesale.
- **Baked states in outputs**: every image queued through the app carries the queue-time composer state as a `tagcomposer_state` PNG text chunk (written by the save node when `embed_workflow` is on). Dropping such a PNG anywhere on the composer page restores that state and pins the image as the preview. See [[ComfyUI Integration]].

## Keyboard shortcuts

| Key | Effect |
| --- | --- |
| **Ctrl+Z** | Undo |
| **Ctrl+Shift+Z** | Redo |
| **Down** (search bar, empty) | Focus first tag row |
| **Up / Down** (in groups) | Move row selection |
| **Delete** (in groups) | Remove selected row's tag |
| **Esc** (search bar) | Return focus to groups |
| Printable char (in groups) | Focus selected row's inline editor with the keystroke |

Run / Interrupt / Clear-pending are also exposed as ComposerScrollArea key bindings, so they fire even when focus is inside the groups list. The exact bindings are part of the page's `ComposerScrollArea` widget; see [[Keyboard Shortcuts]] **(stub)** for the canonical list.

## Coordination with other pages

- **[[Tile View]]** drives entry pushes; the composer mirrors push state back so the green borders update.
- **[[Workflow Editor]]** owns the var definitions for the currently selected workflow; the composer just consumes them.
- **[[Facet Editor]]** is where the `?` undefined-toggle's targets get triaged - right-click a tag and choose "Edit facets" to land directly on its row.
- **[[Prompt History]]** records every push the composer makes and lets you restore back to any previous state (composer + workflow vars + LoRAs) with one click.
- **[[Danbooru Wiki]]** receives wiki lookups from any "Wiki" context-menu action.

## Performance notes

- Pipeline pushes are coalesced by `queueRepush()` (see [above](#coalescing-bursts-of-tag-additions)). This is what keeps comma-paste in the entry-panel tag search bar from lagging the UI - all the per-tag signals collapse into one pipeline run.
- Big rebuilds (search filter, undefined toggle, state restore, manual add) fade the center stack opacity for ~180 ms instead of flashing - hides the rebuild flicker.
- Rules and vars sidebar lists rebuild their layout on every change; the per-row widgets are cheap (one line edit + a button or two), so even with 100+ rules / vars the rebuild is sub-frame.
