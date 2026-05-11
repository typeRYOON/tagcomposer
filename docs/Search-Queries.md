# Search Queries

The tile-view search bar uses a single grammar for everything from "find a tag" to multi-clause triage queries. This page is the full reference.

## Quick example

```
red hair, blue eyes, -loli, title:miku, sort:title:asc
```

Returns entries that:

- have tags matching `red hair` AND `blue eyes` (both prefix-matched against the tag index),
- do NOT contain a `loli` tag,
- have `miku` somewhere in the entry title,
- sorted by title, ascending.

## Grammar

```
QUERY  := GROUP ('|' GROUP)*
GROUP  := CLAUSE (',' CLAUSE)*
CLAUSE := <tag>                  positive tag (prefix match)
       | -<tag>                  negative tag
       | title:<term>            substring on entry title
       | comment:<term>          substring on entry comment
       | lora:<term>             substring on LoRA filename or sha256 prefix
       | sort:<key>[:asc|:desc]  global; last one wins
       | images:[op]<n>          image-slot count
       | tags:[op]<n>            total tag count across all images
       | has:<field>             field is non-empty
       | missing:<field>         field is empty
```

`[op]` is `>`, `<`, `>=`, `<=`, `=`, or omitted (= equality).

`<field>` for `has:` / `missing:` is one of `lora`, `title`, `comment`.

## OR with pipe

`|` separates groups. The final result is the union of each group's matches, deduped by entry.

```
red hair|blue hair
```

Returns entries with `red hair` OR `blue hair`.

```
red hair, -genshin impact | blue hair, title:azur
```

Returns entries matching (red hair AND NOT genshin impact) OR (blue hair AND "azur" in title).

```
taihou|st. louis|essex
```

Returns all three characters' entries.

Empty groups (trailing `|`, doubled `||`) are skipped silently.

> **Edge case:** the Danbooru tag `| |` (a face emoticon often used for chibi-style entries) is not searchable through the bar because `|` is always parsed as the OR separator. Workaround: search by a related character/style tag, or pick the tag from the entry panel directly.

## Tag matching

Positive tag clauses use prefix matching via the tag index. `genshin` matches `genshin impact`, `genshin_unrated`, etc. Multiple positive tags in one group are AND'd (intersection).

Negative tags (`-tag`) drop any entry that contains a matching tag.

## Modifiers

### `title:<term>`

Case-insensitive substring against the entry title. Per-group filter.

```
title:taihou           entries whose title contains "taihou"
red hair, title:azur   red-hair entries whose title contains "azur"
```

### `comment:<term>`

Case-insensitive substring against the entry's free-form comment / notes field. Per-group filter.

```
comment:wip            entries with "wip" anywhere in their notes
```

### `lora:<term>`

Substring match against the entry's LoRA filename basename (case-insensitive), or a sha256 prefix.

```
lora:miku              entries whose LoRA filename contains "miku"
lora:abc12345          entries whose LoRA sha256 starts with abc12345
```

### `sort:<key>[:dir]`

Sorts the final result across all OR groups. If multiple groups specify `sort:`, the last one wins.

| Key                          | Description                            | Default direction       |
| ---------------------------- | -------------------------------------- | ----------------------- |
| `title`                      | Entry title (case-insensitive)         | asc                     |
| `images`                     | Image slot count                       | desc                    |
| `tags`                       | Total tag count across all images      | desc                    |
| `created` (alias `date`)     | Creation time                          | desc (newest first)     |

Directions: `asc`, `desc`. Omit for the key's default.

```
sort:title             title ascending (default)
sort:title:desc        title descending
sort:tags:asc          least-tagged first
```

### `images:[op]N` and `tags:[op]N`

Numeric comparators against the image slot count, or the total tag count across all images.

```
images:0               no images
images:>3              more than 3 images
images:<=2             two or fewer images
tags:>=50              at least 50 tags total
tags:<10               under 10 tags total
```

Operators: `>`, `<`, `>=`, `<=`, `=`, or omitted (= equality).

Invalid values (`images:abc`, negative numbers) silently ignore the clause rather than dropping every entry.

### `has:<field>` and `missing:<field>`

Boolean presence checks. Supported fields:

| Field     | `has:` means                     | `missing:` means       |
| --------- | -------------------------------- | ---------------------- |
| `lora`    | entry has a LoRA assigned        | no LoRA assigned       |
| `title`   | title is non-empty               | title is empty         |
| `comment` | comment is non-empty             | comment is empty       |

Last directive on a given field wins.

```
has:lora                    entries with a LoRA
missing:title               entries with empty title
missing:lora, has:comment   no LoRA but has a comment
```

Unknown fields (typo'd `has:foo`) are silent no-ops rather than filter errors.

## Triage recipes

A few combinations for keeping a library clean:

```
missing:title                untitled entries
tags:<5, missing:title       under-tagged AND untitled
tags:>200                    over-tagged candidates
has:lora, missing:comment    tagged LoRAs without notes
```

## More combination examples

```
red hair, blue eyes, -loli
```

Both tags, excludes loli content.

```
genshin, sort:tags:desc
```

Genshin entries, most-tagged first.

```
miku|teto, has:lora, sort:title
```

Either character; must have a LoRA; sorted by title. Note that `has:lora` and `sort:` are written in only the LAST group but apply globally for `sort:`; `has:lora` is per-group so it applies to whichever group it appears in. Be careful with this distinction:

- Per-group filters: `title:`, `comment:`, `lora:`, `images:`, `tags:`, `has:`, `missing:`, positive/negative tags
- Global filters: `sort:` only

If you want `has:lora` to apply to BOTH `miku` and `teto`, repeat it:

```
miku, has:lora | teto, has:lora, sort:title
```

```
red hair, lora:illustrious | blue hair, lora:nai
```

Two LoRA-family buckets unioned.

```
images:0, missing:title
```

Find your blank stubs.

## Scoping cheat sheet

| Clause                  | Scope     |
| ----------------------- | --------- |
| `<tag>` / `-<tag>`      | per-group |
| `title:`                | per-group |
| `comment:`              | per-group |
| `lora:`                 | per-group |
| `images:` / `tags:`     | per-group |
| `has:` / `missing:`     | per-group |
| `sort:`                 | global (last-wins) |
