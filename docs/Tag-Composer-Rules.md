# Tag Composer - Rules

Rules are the part of the pipeline that *changes* the tag set: drop tags, inject new ones, swap one for another, attach a label, or hard-delete from the composer entirely. Stored in `data/system/rules.fct` and live-editable from the composer sidebar.

A rule is `(match expression) -> (action)`. The match expression decides *which* tags the rule fires on; the action decides *what happens* to them (and, for some actions, what new tags get injected).

> **Screenshot suggestion:** the composer's RULES sidebar section with three or four rules visible - one enabled toggle on, one off, one with a force flag, one with the inline `Add` argument editor expanded. Use as the page header image.

## File format

Each rule is a `@rule` block. Indentation isn't significant; the loader just reads `key = value` lines.

```
@rule MyRuleName
    enabled = true
    force   = false
    match   = anyTag(facets: Hair) AND NOT anyTag(facets: Color)
    action  = replace("long hair")
```

| Field | Required | Default | Notes |
| --- | --- | --- | --- |
| `enabled` | no | `true` | Whether the rule fires. The composer toggles this via the row checkbox. |
| `force` | no | `false` | If true, the action runs even when the match expression had zero hits. Useful for `add` rules that inject context-free tags. |
| `match` | yes (unless `force` is set) | - | OR of AND-groups of `anyTag(...)` clauses, see below. |
| `action` | yes | - | One of `skip`, `delete`, `add(...)`, `replace(...)`, `flag(...)`. |

Lines starting with `#` are comments; blank lines are ignored. Duplicate rule names are dropped (with an error) - first definition wins. Malformed clauses are skipped individually rather than nuking the whole rule.

## Match expression

A `match` value is a boolean expression of `anyTag(...)` clauses joined with `AND`, `OR`, and `NOT`. Precedence is standard: `AND` binds tighter than `OR`; `NOT` binds tightest.

```
match = clauseA AND NOT clauseB OR clauseC AND clauseD
        |________________________|   |__________________|
                  group 1                    group 2
        a tag matches if EITHER group's clauses all hold
```

Two clause forms:

| Clause | Matches when... |
| --- | --- |
| `anyTag(facets: f1, f2, ...)` | the tag carries **all** of `f1, f2, ...` in its facet definition. Multiple facets are AND-ed (a tag must have all of them). |
| `anyTag(name: "pattern")` | the tag's name matches the glob. `*` is "any chars", `?` is "any single char". Case-insensitive. |

Both forms can be prefixed with `NOT`. Quotes are required around name patterns; commas separate facets in the facets form.

**The match is per-tag.** The pipeline asks each rule, for each tag in the working set, "does this tag match?" If yes, the action applies to that tag (and possibly injects new ones).

## Actions

Each action produces a specific `RuleResult` on affected tags, which controls how the tag renders in the composer and whether it appears in the output prompt. See [Tag Composer - Tag row anatomy][trow] for the visual mapping; the engine semantics are:

[trow]: Tag-Composer.md#tag-row-anatomy

| Action | What happens to matched tags | What gets added | Output prompt effect |
| --- | --- | --- | --- |
| `skip` | Marked `Skipped` (strike-through, dark dot). | nothing | Tag is excluded from the output. Still visible in the composer so you know what was filtered. |
| `delete` | Marked `Deleted`. Composer **removes from the active set entirely** on next event tick. | nothing | Tag is gone permanently from this session (re-add it manually or via push if you want it back). |
| `add("a", "b")` | Untouched (stay `Include`). | `a` and `b` injected as new `Injected` tags. | The new tags are added to the output. Each picks up its own facets via the FacetIndex. |
| `replace("a", "b")` | Marked `Replaced` (strike-through, red dot). | `a` and `b` injected as new `Injected` tags. | The matched tags drop from the output; the replacements take their place. |
| `flag(label)` | Marked `Flagged`, with the label attached. | nothing | Tag stays in the output. The composer paints an `[label]` badge. Used to *annotate*, not transform. |

Quoting in arguments:

- `add` / `replace` arguments are quoted strings: `replace("tag with space", "other tag")`.
- `flag` takes a bare label (no quotes): `flag(NSFW)`.
- `skip` / `delete` take no arguments.

## The `force` flag

Without `force`, a rule that matches zero tags does nothing - actions only run on the matched set. With `force = true`, the action still runs even with zero matches. The main use case is **conditional `add`**: "if the composer has *no* hair-color tag yet, force-add `brown hair`".

Express this as `match = NOT anyTag(facets: Hair, Color)` with `action = add("brown hair")` and `force = true`. The NOT alone wouldn't be enough - without force, the match-set is empty and the rule does nothing. Force flips that.

The convention in the sidebar: rules with `force = true` get a small flag badge so you can spot them quickly.

## Examples

### Drop "search-only" tags before they reach the prompt

A `SearchOnly` facet marks tags you use to *find* entries (e.g. `wip`, `fav`, `to-refine`) but never want to include in a generated prompt. One rule disposes of them all:

```
@rule RemoveSearchOnlyTerms
    enabled = true
    match   = anyTag(facets: SearchOnly)
    action  = delete
```

`delete` (not `skip`) - the tag has no business being in `m_activeTags` at all.

### Replace a category wholesale

"Whatever headwear is in the prompt, swap it for `no headwear`." Useful when you want to *negate* a category without listing every possible tag.

```
@rule NoHeadwear
    enabled = true
    match   = anyTag(facets: Headwear) OR anyTag(facets: Headpiece) OR anyTag(facets: Headgear)
    action  = replace("no headwear")
```

Three OR clauses cover headwear/headpiece/headgear sub-categories. The single-arg `replace("no headwear")` removes all matches and injects one substitute.

### Force a property when a category is present

"If the prompt has any breast-size tag, lock it to `gigantic breasts`." Force is on so the rule fires regardless of how many breast-size tags happened to be in the input.

```
@rule ForceBreastSize
    enabled = true
    force   = true
    match   = anyTag(facets: Breasts, Size)
    action  = replace("gigantic breasts")
```

The clause `anyTag(facets: Breasts, Size)` requires both facets on the same tag - matches `huge breasts`, `small breasts`, etc.

### Conditional add (requires `NOT` + `force`)

"If no eye-color tag is present, force-add `blue eyes`."

```
@rule DefaultEyeColor
    enabled = true
    force   = true
    match   = NOT anyTag(facets: Eyes, Color)
    action  = add("blue eyes")
```

Without `force` this would never fire (no matches to act on); with force it fires whenever the negated condition holds.

### Name-pattern matching for ad-hoc cleanup

"Drop every tag that ends in `_wip` or `_draft`."

```
@rule DropWipDrafts
    enabled = true
    match   = anyTag(name: "*_wip") OR anyTag(name: "*_draft")
    action  = delete
```

Useful when your tag set has working/draft variants you forgot to clean up.

### Combination: AND + NOT

"If there are any open-eye tags AND no closed-eye tags, replace with `closed eyes`."

```
@rule ConvertOpenToClosedEyes
    enabled = false
    force   = true
    match   = anyTag(facets: OpenEye) AND NOT anyTag(facets: ClosedEye)
    action  = replace("closed eyes", "^ ^")
```

Two-arg `replace` injects both `closed eyes` and the `^ ^` expression tag.

### Flag without changing the prompt

"Mark anything with a `NSFW` facet so I can see it in the composer." The output prompt is unaffected.

```
@rule MarkNSFW
    enabled = true
    match   = anyTag(facets: NSFW)
    action  = flag(NSFW)
```

The composer paints an `[NSFW]` badge on every matched row.

## Tips

- **Rule order matters.** Rules run top-to-bottom and the working tag set carries state between them. A `replace` rule that fires early can inject a tag that a later rule then picks up. Take advantage of this by ordering chain-of-transformation rules deliberately.
- **`skip` vs `delete`.** Use `skip` when you want the tag to keep showing in the composer (as feedback for what got filtered) but stay out of the prompt. Use `delete` when the tag's presence in `m_activeTags` is itself the bug - it'll be removed from session state on the next pipeline pass.
- **Inject rules pick up facets on the new tags.** When `add`/`replace` injects `gigantic breasts`, the engine asks the FacetIndex for that tag's facets and re-bins it correctly. So an injected tag participates in groups, the `?` undefined-toggle, and *later* rules just like a user-added one. (It does **not** itself feed back into earlier rules - only forward.)
- **Edit `add`/`replace` arguments in the sidebar.** The composer's RULES section gives each `add` and `replace` rule an inline per-arg line edit + `✕` + trailing `add tag…`. Edits persist to `rules.fct` on focus loss, snapshot for undo, and trigger a repush. No editor round-trip needed for tweaking arguments.
- **Use `force = true` sparingly.** Most rules are reactive ("if X, do Y") and the default behavior is right. Force is for the cases where the *absence* of something is the signal (combined with `NOT`), or where you want a context-free `add` to run on every push.
- **Name globs are case-insensitive.** `anyTag(name: "*EYES*")` matches `red eyes` and `closed eyes` identically. Tag names get normalized to space form (`red_eyes` -> `red eyes`) before glob matching.
- **Empty facet list in a clause never matches.** `anyTag(facets:)` with no facets fails to parse; even if you slipped one through it would silently match nothing. Always list at least one facet.
- **The "force" badge in the sidebar is a tell.** Glancing at the sidebar tells you which rules will fire even on an empty composer - those are usually the ones that gate global properties (background color, breast size, ...) and you'll want to know which are active before queueing a Run.

## Workflow

The typical edit cycle for rules:

1. Toggle rules on/off in the composer sidebar - that's the fast path. Each toggle re-runs the pipeline (debounced) so you see the effect live.
2. Edit `add`/`replace` arguments inline in the sidebar - same fast path.
3. For structural changes (new rule, new match clause, change action type, ...) open `rules.fct` via the sidebar's external-file icon, edit, hit the reload icon. The composer re-reads the file and rebuilds the sidebar without restarting the app.

The reload is non-destructive: rule enable states from disk overwrite the live composer state, so be aware that toggling rules in the UI without saving and then hitting reload will revert your toggles.

## See also

- [[Tag Composer]] - the page that consumes these rules.
- [[Tag Composer - Groups]] - sibling file that decides which *bucket* each tag appears under (post-rule).
- [[Facet Editor]] - the surface for declaring facets on tags, which drives `anyTag(facets: ...)` clauses.
