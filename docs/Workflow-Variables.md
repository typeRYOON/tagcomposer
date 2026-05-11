# Workflow Variables

The full reference for placeholder substitution in workflow JSON templates: what tokens exist, where they come from, and what kind of value lands at the call site.

A workflow's JSON template (the file under `data/workflows/`) is plain ComfyUI workflow JSON with `__placeholder__` tokens scattered through it. At Run time, TagComposer rewrites those tokens into concrete values before posting to ComfyUI's `/prompt` endpoint. The values come from two places:

- **User-declared variables** in the [[Workflow Editor]] - per-workflow, per-token, eight types.
- **Built-ins** - hard-coded handlers for the positive prompt and the LoRA stack. Always present; nothing to declare.

## The substitution pipeline

Three passes run in order, every Run:

| Pass | Source | What it touches |
| --- | --- | --- |
| 1. `applyToJson` | `WorkflowManager` walking the declared variables | Each declared variable's placeholder (or the two `LatentSize` tokens). Wildcards skipped. |
| 2. `applyPositive` | The composer's rendered positive prompt | `__positive__` only. |
| 3. `applyLoraStack` | The healed active LoRA stack | `__lora_count__`, `__lora_name_N__`, `__lora_wt_N__`, `__lora_model_str_N__`, `__lora_clip_str_N__` for slots 1..10. |

Each pass is a plain string replace; later passes see whatever earlier passes produced. So if a built-in token ends up inside a user var's value, the later pass will still substitute it. In practice you won't trip on this.

After all three passes, `JSON.parse` runs on the result. A failed parse logs an error and aborts the post.

## User-declared variables

Declared per-workflow on the [[Workflow Editor]]'s left column. Each variable has:

- A **placeholder** string (`__seed__`, `__steps__`, ...) - what the workflow JSON references.
- A **type** - decides the substitution shape (raw number, JSON string, two tokens, ...) and the editor UI.

The placeholder is matched against the template literally - case-sensitive, including the leading and trailing `__`. By convention everything in this app uses the `__name__` shape, but the engine doesn't enforce that; `<<seed>>` or `{seed}` would work equally well as long as you spell it the same way in your workflow JSON.

### Type-by-type substitution

| Type | Substitutes as | Example replacement | Notes |
| --- | --- | --- | --- |
| **Seed** | raw 64-bit integer | `1234567890` | Three behavior modes: Fixed (constant), Increment (`++seedValue` per iteration), Randomize (fresh random per iteration, written back to the editor so you can see what was used). |
| **String** | JSON-quoted string | `"masterpiece, best quality"` | Properly escaped: `"`, `\`, `\n`, `\t`, control chars all handled. Author's template can have either `__neg__` or `"__neg__"` - the substitution adds its own quotes either way. |
| **Integer** | raw integer | `30` | Full `int` range. Goes into your KSampler `steps` field or similar. |
| **Float** | raw float, 6 decimals | `7.500000` | Range +/- 1e9. Use for CFG, denoise strength, etc. |
| **DirSearch** | JSON-quoted path | `"sd_xl_base_1.0.safetensors"` | The selected file's path **relative to the search directory**, with `/` swapped for `\\` to match ComfyUI's checkpoint-loader path conventions. Empty selection writes `""`. |
| **LatentSize** | two raw integers (width + height) | `1024` and `1024` | Uses two configurable tokens (`latentWidthToken`, `latentHeightToken`), not the main placeholder. See [LatentSize](#latentsize-two-tokens-one-preset) below. |
| **Image** | JSON-quoted path | `"tagcomposer/<uuid>.png"` | References the [[ComfyUI Integration]] image input cache. Empty uuid writes `""`. The image gets uploaded to ComfyUI's `input/` before the Run posts. |
| **Wildcard** | nothing (bypassed) | - | Wildcards don't substitute a token directly. They're picked at Run time and folded into `__positive__`. See [Wildcards](#wildcards-merged-into-the-prompt) below. |

### Seed: three modes, side effects

```
Fixed       | seedValue is sent verbatim every Run.
Increment   | seedValue++ each iteration, written back to workflows.json.
Randomize   | fresh 63-bit random each iteration, written back so the
            | editor shows the value that was actually sent.
```

The write-back is the bit to notice: a Randomize seed lands on a real number you can see in the editor after the Run. Useful for "I got a good output, what seed was that?" - just look at the seed field right after the Run completes.

Note that **re-queue from [[Prompt History]] doesn't advance the seed**. The history stores the rendered JSON (the seed already baked in), so a replay sends byte-for-byte the same number. Use the composer's Run if you want a fresh seed.

### String: JSON-escaped

`String` vars go through `jsonStringLiteral` so any value is safe to drop into the JSON wholesale - quotes, backslashes, newlines all escaped. This means you can drop a multi-line prompt with embedded quotes into a String var and it'll land correctly:

```
Value: He said "hello"
Replaces as: "He said \"hello\""
```

### DirSearch: relative path + backslash escape

The selected file's path is stored relative to the search directory, and the substitution swaps forward slashes for `\\` (single backslash in the on-the-wire JSON; doubled to escape the JSON string syntax):

```
Search dir:    D:/models/checkpoints
Selected file: D:/models/checkpoints/sdxl/anime.safetensors
Replaces as:   "sdxl\\anime.safetensors"
```

Why backslash: ComfyUI's `CheckpointLoaderSimple` and similar nodes on Windows expect this. On Linux ComfyUI the same path works because ComfyUI normalizes internally.

### LatentSize: two tokens, one preset

A `LatentSize` variable doesn't substitute a single placeholder - it substitutes **two**, configured separately on the variable card:

| Field | Token | Substitutes as |
| --- | --- | --- |
| Width token | `__latent_w__` (or whatever you name it) | raw integer width |
| Height token | `__latent_h__` (or whatever you name it) | raw integer height |

The preset list comes from `data/system/latent_sizes.txt` (format: `width height` per line, `#` for comments, blank lines ignored). The card's main placeholder field is **label-only** - it never gets substituted; it's just a name you see in the editor.

In your workflow JSON, drop the two tokens into the `EmptyLatentImage` node's `width` and `height` keys:

```
"width":  __latent_w__,
"height": __latent_h__,
```

Substitutes to:

```
"width":  1024,
"height": 1024,
```

Both are raw integers, not strings - don't quote them in the template.

### Image: input cache reference

An `Image` var stores a uuid into the [[ComfyUI Integration]] input cache. The substitution writes the relative path ComfyUI sees:

```
"tagcomposer/<uuid>.png"
```

`tagcomposer/` is a fixed subfolder under ComfyUI's `input/` - the app uploads there to stay out of ComfyUI's `clipspace/`. The `<uuid>.png` filename uses the cache's stable id, so the same image always lands at the same path.

If the var has `ImageEdits` (crop, mask, trim) - configured via the [[Clip Editor]] modal - the **edited** PNG is the one that gets uploaded; the path stays `tagcomposer/<uuid>.png` regardless. Edit variants are cached separately under `_edited/<editsHash>/` in the cache; masks under `_masks/<maskId>.png`.

### Wildcards: merged into the prompt

Wildcard variables don't have a substitution token. At each iteration of Run, the app calls `pickWildcardTags()` which:

1. Walks all `Wildcard` variables.
2. For each one with a non-empty `wildcardTags` list, picks a random line.
3. Splits the line on commas, trims each piece.
4. Returns the flat list.

The result is unioned with the composer's active tags before the positive prompt is rendered. So a wildcard line of `blue hair, blue eyes` contributes two tags to that one iteration's prompt.

Multi-iteration Runs re-pick per iteration, so a `count = 4` Run over a 10-line wildcard fires four prompts each with a fresh random pick.

## Built-ins

These run unconditionally on every Run. The tokens don't have to be declared anywhere - if they're in the template, they get substituted; if they're not, nothing happens. (You can omit `__positive__` from an upscale workflow and the Run still succeeds.)

### `__positive__`

```
Substitutes as: "<the rendered positive prompt>"
Source:         The composer's currentPromptString (or computePromptWithExtraTags for batch / wildcard runs).
```

The substitution adds JSON quotes - the template can write either `"__positive__"` (already quoted, common) or bare `__positive__` (the substitution provides the quotes). Both forms work and produce the same result.

The prompt itself is pre-escaped before this pass runs - quotes, backslashes, etc. inside the prompt are already in their JSON-escaped form.

### `__lora_count__`

```
Substitutes as: <integer>
Source:         The healed active LoRA stack's size.
```

Raw integer. Goes into nodes that conditionally chain LoRAs (e.g. "build a chain of N loaders, then stop"). With zero LoRAs active, substitutes to `0`.

### `__lora_name_N__` (N = 1..10)

```
Substitutes as: "<lora file path>" or "None"
Source:         The LoRA stack's slot N (1-indexed), or "None" if the slot is empty.
```

JSON-quoted string. The file path goes through the same `/` -> `\\` swap as `DirSearch` for Windows compatibility. Empty slots write the literal string `"None"` - ComfyUI's `LoraLoader` treats that as a no-op LoRA.

All 10 slots are substituted on every Run, regardless of how many LoRAs are actually active. So slot 6 in a 10-slot template gets `"None"` when only 3 LoRAs are stacked.

### `__lora_wt_N__` (N = 1..10)

```
Substitutes as: 1.000000
Source:         Hard-coded.
```

Always `1.000000`, whether the slot is filled or not. Present as a forward-compat hook; current behavior leaves the per-LoRA weight as a per-strength field via the next two tokens. If your workflow has a "global LoRA weight" knob you want to wire to per-LoRA values, this is the token to repurpose; today it's effectively a no-op.

### `__lora_model_str_N__` and `__lora_clip_str_N__`

```
Substitutes as: <float, 6 decimals>
Source:         The LoRA's modelStr / clipStr (filled slots), or defaults (0.9 / 2.0) for empty slots.
```

The per-LoRA strength values, edited on the [[Workflow Editor]]'s middle column or the [[Tile View]] entry panel. Six-decimal precision matches ComfyUI's internal float formatting.

Empty slots use 0.9 / 2.0 as defaults so a partially-filled stack still produces a valid JSON parse - the unused slots are silently zero-impact via the `"None"` lora name.

### LoRA template layout

A typical workflow's LoRA section looks something like:

```
"4": {
    "inputs": {
        "lora_name": __lora_name_1__,
        "strength_model": __lora_model_str_1__,
        "strength_clip": __lora_clip_str_1__,
        "model": [...],
        "clip": [...]
    },
    "class_type": "LoraLoader"
},
"5": {
    "inputs": {
        "lora_name": __lora_name_2__,
        ...
    },
    ...
},
```

One `LoraLoader` node per slot, daisy-chained through `model` / `clip` inputs. The `"None"` lora-name handling lets you size the chain once for the worst case and have empty slots be free.

If you want fewer slots than the default 10, omit the higher-numbered tokens from the template - the substitution still runs (replaces `__lora_name_5__` with `"None"` even if no node references it), but `JSON.parse` doesn't see it because no node uses it. Slot count is template-defined, not configured here.

## Validation at Run time

Before any prompt is posted, [[ComfyUI Integration]] runs `workflowTemplateIssues` which checks:

1. **Unused user vars** - every declared variable's placeholder (or `LatentSize` tokens) must appear in the template.
2. **Stray dunder tokens** - any `__name__` in the template must correspond to a known handler (declared user var, or one of the built-ins above).
3. **LoRA slot coverage** - if N LoRAs are active, `__lora_name_1__` through `__lora_name_N__` must all be in the template.

See [[ComfyUI Integration#run-block-validation]] for the exact rules and block-message format. Built-in tokens never count as "unused" - `__positive__` can be safely omitted (upscale workflows do this), and the LoRA built-ins are conditionally required based on the active stack.

## Naming and convention

The engine doesn't impose a placeholder shape - any string a user var stores in `placeholder` is matched literally. But for consistency:

- **Use `__name__` (double underscore wrap)** - matches the validation regex (`__[A-Za-z0-9_]+?__`), the built-ins (`__positive__`, `__lora_*__`), and the editor's default placeholder for new vars.
- **Lowercase + underscores** for multi-word names: `__model_steps__`, `__neg_prompt__`, `__refiner_cfg__`. Mixing case works but reads poorly next to the built-ins.
- **Don't shadow built-ins.** Naming your own var `__positive__` works (the user-var pass runs first and would substitute it) but breaks the moment you swap the workflow to one that uses the actual built-in. Avoid `__positive__`, `__lora_*__`, and `__lora_count__` as user-var names.
- **Latent Size's tokens are NOT the main placeholder.** Don't name your LatentSize variable's placeholder `__latent_w__` thinking it does anything - it's label-only. The two real tokens are configured in their own fields.

## Tips

- **Open the workflow JSON before declaring vars.** Click the open-external icon on the [[Workflow Editor]]'s header to find the placeholders that already exist in the template, then declare them here verbatim. Beats guessing.
- **Use the validation to catch typos.** A typo in either direction (template has `__steps__`, var declared as `__step__`) blocks the Run with a clear message. Read it before re-declaring.
- **Wildcards bypass active tags.** Don't try to "see what wildcard picked" by checking the composer - use [[Prompt History]] to see the positive prompt that was actually sent.
- **LatentSize aspect-ratio preview.** The editor card shows a small rectangle next to the size list so you can eyeball portrait vs landscape vs square without doing arithmetic.
- **Increment seed + count = N gives sequential seeds.** Great for grids: the Increment mode bumps the seed once per iteration, so a `count = 4` Run produces seeds N, N+1, N+2, N+3.
- **DirSearch's path is relative.** Move the search directory and the selected file path stays valid - what changes is the absolute resolution. Useful for portable data folders.
- **The LoRA stack is sized for 10 slots.** If you usually run 1-2 LoRAs, you don't need 10 `LoraLoader` nodes - shorten the chain to your actual max. The substitution doesn't care if you reference fewer.

## See also

- [[Workflow Editor]] - declares variables and tunes the LoRA stack.
- [[ComfyUI Integration]] - the run-time pipeline (validation, upload, dispatch).
- [[Tag Composer]] - source of `__positive__`.
- [[Tile View]] - source of the active LoRA stack.
- [[Prompt History]] - records every rendered JSON so you can see what actually shipped.
- [[Clip Editor]] - the modal that produces the `ImageEdits` payload for Image-type variables.
