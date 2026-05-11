# Tag Composer - Groups

The category buckets in the [[Tag Composer]] center column (Body/Hair, Clothing/Headwear, Action/Pose, Quality, ...) come from a single text file: `data/system/groups.fct`. A group decides *which heading a tag is shown under*; the actual prompt-effecting decisions live in [[Tag Composer - Rules]] and `tag_definitions.fct`.

> **Screenshot suggestion:** the composer center column scrolled so several group headers are visible (`Body/Hair`, `Body/Eyes`, `Clothing/Headwear`, ...) plus the floating category nav panel expanded on the right. Use as the page header image.

## File format

The grammar is two lines per group:

```
@group <DisplayName>
    facet1, facet2, ...
```

A tag is placed in a group if **all** of the group's facets are present in the tag's facet set. Groups are walked top-to-bottom; **first match wins**. Tags that match no group fall through to a synthetic "Uncategorized" bucket at the bottom.

Lines starting with `#` are comments. Blank lines are ignored. Display names can include `/` for visual hierarchy (e.g. `Body/Hair`) - that's purely typographic, not structural; nothing in the loader splits on `/`.

Minimal example:

```
@group Style
    rStyle

@group Character
    rCharacter

@group Body
    rBody
```

## Routing facets (convention)

There's nothing in the engine that says facets need to be of a particular shape - any string a tag is annotated with works. But this app uses a strong **convention** to keep groups manageable: tag definitions carry two *kinds* of facets, and each plays a distinct role.

| Facet kind | Prefix | Examples | Purpose |
| --- | --- | --- | --- |
| **Routing facet** | starts with `r` | `rBody`, `rClothing`, `rActionPose`, `rTriggerWord`, `rStyle`, `rCopyright`, `rEffects`, `rEnvironment`, `rQuality` | Names the top-level *category* the tag belongs to. One per tag (or a small number). |
| **Leaf facet** | no `r` prefix | `Hair`, `Eyes`, `Headwear`, `Breasts`, `Color`, `Size`, `Pose`, `Bangs`, `Tail`, `Topwear`, ... | Names a property of the tag. A tag carries as many leaf facets as fit (a "red hair" tag has both `Hair` and `Color`, for instance). |

A typical tag definition for `red hair` then carries:

```
red hair: rBody, Hair, Color
```

The routing facet `rBody` says "this is a body trait"; the leaf facets `Hair` and `Color` describe what *kind* of body trait.

### Why this matters for `groups.fct`

Once you adopt the routing-facet convention, `groups.fct` becomes a small two-level decision tree: routing facet first, then optional leaf-facet specializations.

```
# Most specific first - first match wins.
@group Body/Hair
    rBody, Hair

@group Body/Eyes
    rBody, Eyes

@group Body/Breasts
    rBody, Breasts

# Catch-all fallback for any rBody tag the specific groups don't claim.
@group Body
    rBody
```

The ordering rule is the key: list the multi-facet groups *before* the single-routing-facet fallback. `red hair` (carrying `rBody, Hair`) matches `Body/Hair` first and never reaches `Body`. `body sweat` (carrying `rBody` but no `Hair`/`Eyes`/`Breasts`/...) falls past every specific group and lands in `Body`.

### Why the prefix at all?

Two reasons the `r` convention earns its keep:

1. **Visual separation in the facet editor.** When you're staring at a tag definition, you can tell at a glance which facet is the category vs which ones describe properties. A tag with `rBody, Hair, Color` reads "Body-routed, hair, color" - the `r` is doing real work as a sigil.
2. **Avoids collisions with leaf names.** Without the prefix, a routing facet `Body` would collide with a future leaf facet that wants to be called `Body` (and the routing semantics are different - one decides the *bucket*, the other decides a *property within* a bucket). Prefixing the routing form sidesteps the ambiguity permanently.

You're free to ignore the convention if your tag library is small and your groups are flat. The engine doesn't know or care about the `r` prefix.

## Worked examples

### Two-level hierarchy

```
@group Body/Hair/Color
    rBody, Hair, Color

@group Body/Hair
    rBody, Hair

@group Body
    rBody
```

Order matters here: `pink hair` (with `rBody, Hair, Color`) lands in `Body/Hair/Color` because all three facets match; `long hair` (with `rBody, Hair`) skips the color group and lands in `Body/Hair`; `back arched` (with `rBody`) skips both and lands in `Body`.

### Special "Trigger Word" group at the top

```
@group TriggerWord
    rTriggerWord

@group Style
    rStyle

@group Subject
    rSubject
```

`rTriggerWord` is a routing facet you'd attach to character/copyright LoRA trigger words (e.g. `1girl, miku_v1, hatsune miku`). Putting it first means LoRA triggers always render at the top of the prompt - convenient for prompt structure.

### Splitting "Action" into pose subgroups

```
@group Action/Pose/Arms
    rActionPose, Arms

@group Action/Pose/Legs
    rActionPose, Legs

@group Action/Pose
    rActionPose
```

A tag like `arms behind back` (with `rActionPose, Arms`) goes to `Action/Pose/Arms`. A tag with just `rActionPose` (e.g. `dynamic pose`) falls through to `Action/Pose`.

## Tips

- **Always include a catch-all per routing facet.** Without `@group Body { rBody }` at the end of the Body family, any tag that has `rBody` but doesn't hit a specific subgroup falls into Uncategorized - usually not what you want.
- **Order specificity-first.** The matcher is greedy in the order you write groups, not in the number of facets. A 3-facet group placed *after* a 1-facet group with one of those facets is unreachable.
- **Group display names are arbitrary strings.** Use `/` for readable nesting but don't expect any folder behavior - it's purely cosmetic. The composer just sorts groups by their first occurrence in the file.
- **Keep routing facets shallow.** A tag should usually have **one** routing facet. Two routing facets is a sign your routing taxonomy isn't crisp; consider whether the tag belongs in one bucket or whether you need a new routing facet.
- **Leaf facets can be reused across routes.** `Color` is useful under both `rBody` (hair color, eye color) and `rEnvironment` (background color), and that's fine - `Color` alone never names a group, it always co-occurs with a routing facet that does.
- **The facet schema lives in a different file.** Group definitions reference facet names but don't declare them - the canonical list of allowed facet names is `data/system/facets.fct`. If a group references a facet that isn't in the schema, no tag will ever carry it and the group is silently dead. The [[Facet Editor]] purge action drops orphaned facets from tag definitions but doesn't touch `groups.fct`.

## Reloading

There's no in-app reload button for `groups.fct` - the file is read once at app start. To pick up edits:

- Save your `groups.fct` changes in the editor.
- Restart the app.

If you find yourself iterating on group definitions a lot, leave the file open in your editor next to the composer and bounce the app after each batch of changes. The composer's per-group display rebuild is fast, so the test loop is mostly the app restart time.

## See also

- [[Tag Composer - Rules]] - the live-evaluated decisions about what tags to keep, drop, or transform.
- [[Facet Editor]] - the surface for declaring facets on individual tags.
- [[Tag Composer]] - how groups appear in the composer UI.
