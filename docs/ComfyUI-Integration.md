# ComfyUI Integration

How TagComposer talks to [ComfyUI](https://github.com/comfyanonymous/ComfyUI). The app sits in front of a running ComfyUI instance and uses it as a generation backend - all of the actual model loading, sampling, and image saving happens on the ComfyUI side.

Four moving parts:

| Piece | Purpose |
| --- | --- |
| **WebSocket** | Subscribes to ComfyUI's job-state stream: in-flight preview images, sampler step progress, queue depth. |
| **HTTP API** | Submits prompts, interrupts the current job, clears the queue, frees model memory, uploads input images. |
| **Image input cache** | App-side store of every workflow-input image (and its edits), with lazy upload to ComfyUI's `input/` folder. |
| **Output / temp folders** | App reads ComfyUI's output tree directly for the [[Output Viewer]] and the inline final-image preview. |

## Connection setup

Configured in [[Settings]]. Four fields drive everything:

| Setting | Purpose |
| --- | --- |
| `comfyUiEnabled` | Master switch. When off, the WS isn't connected and run buttons are no-ops. |
| `comfyUiServerAddress` | `host:port`, **no scheme prefix**. Example: `127.0.0.1:8188` (the app prepends `http://` / `ws://` itself). |
| `comfyUiApiKey` | Optional. When non-empty, every POST includes `extra_data.api_key_comfy_org` so ComfyUI's auth check passes. Leave blank for unsecured local instances. |
| `comfyUiOutputFolder` | Path pattern to ComfyUI's `output/` directory, optionally with a date suffix like `{yyyy-MM-dd}` (resolved per run). Drives the [[Output Viewer]] tree and the new-output watcher. |
| `comfyUiTempFolder` | Flat folder ComfyUI writes in-progress decode images to. App reads this to load the final image into the composer's preview tile after each completed prompt. |
| `comfyUiInputFolder` | **Optional**. ComfyUI's `input/` folder. When set and writable, image inputs are copied directly into it (fast path) instead of being POSTed as multipart form data. |

### Connection lifecycle

`ComfyUiClient` runs its `QWebSocket` on a dedicated `QThread` so HTTP latency or stalls in the worker never freeze the GUI. The client identity is a UUID minted at construction (`m_clientId`); the WS connects as:

```
ws://<comfyUiServerAddress>/ws?clientId=<uuid>
```

The same `clientId` goes into every HTTP `/prompt` body so ComfyUI knows which client to address with progress messages.

State changes propagate via signals: `connected` / `disconnected` / `connectionError(message)`. The [[Settings]] page surfaces an indicator. The status bar tracks queue depth and progress live (see below).

`applyComfySettings` re-reads the four settings fields on save and reconnects only when the host or enable flag actually changed - cosmetic settings like the output folder don't bounce the WS.

## What the WebSocket delivers

Two message types are consumed by the app; everything else is ignored.

### `preview`

Fired periodically while a prompt is being sampled. Carries:

- `step` (0-indexed) and `total_steps` - reported to the composer + status bar (`previewProgressChanged`). Step is +1'd for display, so users see `1 / total` through `total / total`.
- Base64 image payload - decoded into a `QImage` and pushed to the composer's preview tile (`previewImageReady`). The decode happens on the WS thread; only the finished `QImage` crosses to the GUI thread.

The app skips the step-0 frame: ComfyUI re-emits the *previous* generation's final image at step 0 of the next, which would briefly flash the old result.

### `status`

Fired on queue depth changes. Carries the remaining queue count, which:

- Updates the status bar's "active count" widget.
- Sets the composer's preview popout's mirrored badge.
- **Triggers the final-image preview load.** Every queue-count decrement is treated as a finished prompt. After 500 ms (giving ComfyUI time to write the output file to disk), the app scans `comfyUiTempFolder` for the newest image and pins it as the composer's preview tile.

The final-load is suppressed for two cases:

- After an explicit interrupt (`m_skipNextFinalLoad` - the temp-folder write is stale).
- After a "clear pending" (`m_skipFinalOnPendingClear` - only the dropped queue, not a real finish).

## HTTP endpoints

All POSTs target `http://<comfyUiServerAddress>/...` and carry `extra_data.api_key_comfy_org` when the app has an API key configured.

| Endpoint | Body | When |
| --- | --- | --- |
| `POST /prompt` | `{"client_id":"<uuid>", "prompt":{...workflow JSON...}, "extra_data":{...}}` | Every queued prompt (composer Run, batch run, [[Prompt History]] re-queue). The `prompt` field is the rendered JSON with all placeholders already substituted. `extra_data` carries the API key plus the baked composer snapshot (see [Baked composer state](#baked-composer-state-drop-to-restore)). Failed JSON parse logs and aborts. |
| `POST /interrupt` | `{"extra_data":{...}}` | Composer's `■` Interrupt button. Cancels the in-flight prompt. |
| `POST /queue` | `{"clear":true, "extra_data":{...}}` | Shift-click on the Interrupt button (composer + popout). Drops the pending queue without canceling the in-flight job. |
| `POST /free` | `{"unload_models":true, "free_memory":true, "extra_data":{...}}` | Used after specific "release the .safetensors" needs - e.g. when the app wants to overwrite a LoRA file ComfyUI has a handle on. Callback-driven so the caller can sequence its retry. |
| `POST /upload/image` | Multipart: `image` file part + `subfolder=tagcomposer`, `type=input`, `overwrite=true` | Image-input upload fallback when no local `input/` folder is configured. See below. |

All requests are fire-and-forget at the HTTP layer (`QNetworkReply::finished` -> `deleteLater`) except `/free` and `/upload/image`, which take a callback so the caller can react to success/failure.

## Image input upload

Image-typed workflow variables ([[Workflow Editor]]) reference an entry in the **app-side cache** at `data/workflow_inputs/` by `uuid`. The actual PNG that ComfyUI sees lives under ComfyUI's `input/tagcomposer/<uuid>.png`. The app keeps the two in sync via `ensureImageInputsUploaded`.

### Tracking key

Uploads are tracked by `uuid + ":" + editsHash`, where `editsHash` is a stable digest of the active `ImageEdits` (crop / mask / trim). This means:

- Same source image, no edits -> one upload per session.
- Edit the crop or swap the mask -> the rendered edited PNG gets a fresh tracking key, gets re-uploaded.
- Re-running the same prompt -> no re-upload (the key is already in `m_uploadedThisSession`).

The tracking set resets on app restart.

### Fast path: direct copy

When `comfyUiInputFolder` is set and writable (i.e. ComfyUI runs on the same machine as the app), the upload is a plain `QFile::copy` into `<inputFolder>/tagcomposer/<uuid>.png`. No multipart, no HTTP, no decode-on-server overhead. This is the path you want for local ComfyUI.

### Fallback: multipart POST

When `comfyUiInputFolder` is empty (remote ComfyUI, network mount issues, ...), the app falls back to multipart `POST /upload/image`. ComfyUI re-encodes the bytes server-side and writes them into its own `input/` folder.

### Lazy upload before Run

Both the composer Run and the [[Workflow Editor]]'s Batch run wrap their queue logic in `ensureImageInputsUploaded(callback)`. The flow:

1. Walk the selected workflow's image vars, build the upload task list (skipping already-uploaded keys).
2. If the list is empty, fire the callback immediately - no network round-trip.
3. Otherwise dispatch all tasks in parallel, and only fire the callback when every upload completes (or fails - the loop continues either way).

Failed uploads land in the status bar with the first 8 chars of the tracking key and the underlying error message. The Run continues; ComfyUI will reject the prompt if the input file is missing on its side.

## Run-block validation

Before any `queuePrompt` actually fires, two checks run. Either failing aborts the run with a status-bar message; no JSON is sent.

### Unloaded image inputs

`unloadedImageInputs()` walks the selected workflow's variables and collects every `Image` var whose `imageUuid` is empty. An empty uuid means no image has been picked at all - the workflow would render an empty filename token into the JSON, which ComfyUI would reject.

Block message: `Run blocked - image input(s) not loaded: __input_img__, __mask__`.

### Template issues

`workflowTemplateIssues(template, activeLoraCount)` runs three sub-checks against the raw workflow JSON template:

1. **Unused variables.** Each declared workflow variable's placeholder (or both `LatentSize` tokens) must appear in the template. Wildcards are exempt (they fold into `__positive__` and have no direct token). Variables with empty placeholders are flagged too.
2. **Stray dunder tokens.** Any `__name__` regex match in the template that doesn't correspond to a known handler: not a declared variable, not a built-in (`__positive__`, `__lora_count__`, `__lora_name_1..10__`, `__lora_wt_1..10__`, `__lora_model_str_1..10__`, `__lora_clip_str_1..10__`). Catches typos like `__negativ__` and templates that reference variables you forgot to declare.
3. **LoRA coverage.** If `activeLoraCount > 0`, slots `__lora_name_1__` through `__lora_name_N__` must all be in the template. Missing slots mean the user's selected LoRAs would silently get dropped. With zero active LoRAs the check is skipped.

Block message format: `Run blocked - unused variable(s): __steps__; unresolved token(s): __negativ__; 2 LoRA(s) active but missing slot(s): __lora_name_2__`.

For batch runs the wrapper is `Batch blocked - ...`. The worst-case LoRA count is computed by checking whether any matched entry has its own LoRA - if so, `activeLoraCount = global + 1`, since an entry tops up the stack by at most one.

## Run flow

Composer Run (`PromptComposerPage::Run` -> `runRequested` signal -> `AppMainWindow` slot):

1. Read the selected workflow's template file.
2. Run `unloadedImageInputs` and `workflowTemplateIssues` - abort on any hit.
3. `ensureImageInputsUploaded` (callback below).
4. Heal the active LoRA stack against the configured LoRA roots.
5. For `i = 0..count`:
   - `pickWildcardTags()` - random pick per wildcard variable.
   - `currentPromptString()` (no wildcards) or `computePromptWithExtraTags(wildTags)` to build the positive prompt.
   - `applyToJson` - substitutes every declared workflow variable. **Side effect:** Increment seeds advance, Randomize seeds get a fresh value (written back to the editor).
   - `applyPositive` - substitutes `__positive__`.
   - `applyLoraStack` - substitutes `__lora_count__` + the four `__lora_*_N__` families for slots 1..10.
   - `recordAndQueue` - records the rendered JSON in [[Prompt History]] (including a snapshot of the composer state), then POSTs `/prompt`.
6. Save the (now-mutated) workflow vars back to disk so the next launch starts from the same seed state.
7. Refresh the workflow editor so the seed field reflects the bumped value.

Batch flow is the same shape but per-matched-entry, and `recordAndQueue` is called with the entry id as `batchEntryId` so [[Prompt History]] shows which entry contributed.

[[Prompt History]] is the only consumer of the *rendered JSON*; its re-queue button replays the exact bytes, so the seed survives across re-queues even for Randomize / Increment vars.

## Baked composer state (drop-to-restore)

Every `/prompt` POST carries the queue-time composer snapshot - the same `SavedState` JSON that [[Prompt History]] records and named states persist - in `extra_data.extra_pnginfo.tagcomposer_state`. ComfyUI hands `extra_pnginfo` to any save node with a hidden `EXTRA_PNGINFO` input; WAS's **Image Save** with `embed_workflow: true` and the stock **SaveImage** node both write each key as its own PNG `tEXt` chunk. The output PNG ends up with ComfyUI's own `prompt` chunk and a `tagcomposer_state` chunk side by side.

Round trip: drop such a PNG anywhere on the [[Tag Composer]] page. The app reads the chunk straight from the file (raw chunk walk, no pixel decode), rebuilds the `SavedState`, and runs the normal state-restore path - tags, weights, pushes, rules, variables, workflow selection + vars, LoRA stack, and the group / format profile stamp. The dropped image is pinned into the preview tile as feedback. This mirrors ComfyUI's drop-a-workflow-image behavior, but restores the *composer* state instead of the node graph.

Notes:

- **PNG only.** JPEG / WebP outputs have no text chunks; the state rides only on PNG saves.
- **The save node controls it.** `embed_workflow: false` (or a save node without the hidden `EXTRA_PNGINFO` input) drops the state chunk - along with ComfyUI's own `prompt` chunk.
- Re-queues from [[Prompt History]] bake the *original* record's snapshot, so a re-queued image restores the state that actually produced it.
- Images from plain ComfyUI (no TagComposer in the loop) have no `tagcomposer_state` chunk; dropping one reports `No baked composer state in ...` and changes nothing.
- The restore has the same semantics and warnings as clicking a state tile: a missing workflow id or missing entries degrade gracefully with a status-bar note.
- Wildcard picks are per-run and transient, so they are not part of the restored state (same as a [[Prompt History]] restore). The exact tags that landed in the image are in the PNG's `prompt` chunk.
- The profile stamp carries the *resolved* group order and format list, not just the profile names, so an image made under one model convention still restores that convention on a machine whose `profiles.fct` differs. Images baked before profiles existed carry no stamp and leave the active profiles alone.

## Interrupt and clear

| Action | Button | Effect | Final-load suppressed? |
| --- | --- | --- | --- |
| **Interrupt** | Composer `■` (and the preview popout's mirror) | `POST /interrupt`. Cancels in-flight job; pending queue keeps draining. | Yes - the next queue-count decrement is the interrupted job, whose temp image is stale. |
| **Clear pending** | Shift-click the same button | `POST /queue {"clear":true}`. Drops the pending queue but the currently-running prompt finishes naturally. | Final-load is suppressed once for the dropped queue, but the in-flight job still loads its output. |

Both are no-ops when the WS isn't connected (the app logs and bails).

## Output watching

The [[Output Viewer]] is rooted at `comfyUiOutputFolder` (with the date pattern resolved). The composer's inline preview tile uses `comfyUiTempFolder`, scanning for the newest `.png` / `.jpg` / `.jpeg` / `.webp` whenever a job completes. The 500 ms delay between queue-count decrement and the scan gives ComfyUI time to finish writing.

Neither folder is watched continuously - the scan only fires on the queue-decrement edge. This keeps disk I/O bounded and avoids partial-file races on slow filesystems.

## Memory release

`POST /free {"unload_models":true, "free_memory":true}` is exposed as `ComfyUiClient::freeMemory(callback)`. Used in narrow situations:

- The user wants to overwrite a `.safetensors` file ComfyUI has a Windows handle on (e.g. via the [[Workflow Editor]]'s LoRA path picker).
- Cleanup flows that need ComfyUI to release model files before the app moves them.

Not exposed as a user-facing button at the moment; it's a tool for internal flows that need to recover from "the file is in use" errors.

## Tips

- **Set `comfyUiInputFolder` when ComfyUI is local.** Direct-copy upload is several orders of magnitude faster than multipart for large images and dodges ComfyUI's re-encode step entirely.
- **Use the API key field for ComfyUI Cloud / hosted instances.** The header isn't sent if the field is empty, so it's a no-cost field to fill in.
- **The output folder pattern accepts a date suffix.** `C:/ComfyUI/output/{yyyy-MM-dd}` resolves to a fresh subfolder each calendar day. The [[Output Viewer]] navigates to today's folder on launch.
- **Interrupt vs Clear pending is fingers, not menus.** Click for interrupt; Shift-click for clear-pending. Same button.
- **A failed upload doesn't abort the Run.** Watch the status bar after a Run; if you see `Upload failed (abc12345): ...`, ComfyUI will reject the prompt and you'll need to re-Run after fixing the source.
- **The `client_id` is per-app-launch, not per-session.** Two TagComposer instances pointed at the same ComfyUI server can step on each other's preview / status messages because each has its own UUID and ComfyUI multiplexes them. Run one app instance per ComfyUI server.
- **Re-queueing a [[Prompt History]] entry sends the same JSON.** Bumping the seed on the original `WorkflowVar` doesn't affect re-queues - the history stores the rendered JSON, including the seed value used at the time. To get a fresh seed, Run again from the composer.

## See also

- [[Tag Composer]] - the Run button.
- [[Workflow Editor]] - workflow variable declarations + batch run.
- [[Tile View]] - LoRA activation (mirrored into the active stack the run uses).
- [[Prompt History]] - records every queued prompt; re-queue uses the rendered JSON path.
- [[Output Viewer]] - browser for `comfyUiOutputFolder`.
- [[Workflow Variables]] - **(stub)** - the placeholder substitution rules + built-ins.
- [[Settings]] - the UI for the six ComfyUI settings fields.
