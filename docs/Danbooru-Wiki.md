# Danbooru Wiki

An in-app browser for [danbooru.donmai.us](https://danbooru.donmai.us)'s tag wiki pages. Look up what a tag means, what its aliases are, and see a few example posts - without leaving TagComposer.

The page hits `danbooru.donmai.us`'s JSON API directly. No login, no API key required for wiki reads.

> **Screenshot suggestion:** the wiki page open on a character or copyright tag with the title + alias block visible, a paragraph or two of body text, and a row of embedded post thumbnails. Use as the page header image.

## Where lookups come from

The page receives a tag in one of three ways:

| Source | Action |
| --- | --- |
| The search bar at the top of this page | Type a tag, autocomplete, commit -> looks up immediately. |
| Another page's right-click menu | The [[Tag Composer]], [[Tile View]], [[Facet Editor]], and [[Dataset Helpers]] all expose a "Wiki" / "Go to Wiki" right-click action on tag rows that routes through `AppMainWindow` to land here. |
| In-page wiki links | Anchors in the rendered body (linking to other tags) navigate within this page. |

The first two switch to this page (if you weren't already on it) and call `lookupTag`; the third just calls `lookupTag` in place and pushes the previous tag onto the back-history stack.

## Layout

Three rows, edge-to-edge:

1. **Top bar** - back / forward / search bar / open-external. Always visible.
2. **Title block** - title + aliases. Hidden in loading and not-found states.
3. **Body** - the rendered wiki content in a `QTextBrowser`, scrollable.

The body, the loading placeholder, and the not-found placeholder share a `QStackedWidget` so the page-level fade transitions cover all three states uniformly.

## Top bar

| Control | Effect |
| --- | --- |
| **<-** Back | Navigates to the previous tag in the in-app history stack. **Alt+Left** keyboard shortcut. Disabled at the start of the stack. |
| **->** Forward | Counterpart. **Alt+Right** shortcut. Disabled at the end of the stack. |
| **Search bar** | `TagSearchBar` with the standard Danbooru-style autocomplete. Committing a tag (Enter / Tab / click suggestion) triggers `startFadeOutThenLookup` - the page fades out and the next tag fades in once fetched. |
| **Open external** (icon) | Opens the current tag's wiki page on `danbooru.donmai.us` in your default browser. URL-encoded for tag names with special characters. |

The history stack is maintained per-session: a fresh launch starts with an empty stack. Navigating via the search bar or a wiki-link click adds an entry; the back/forward buttons walk the stack without adding entries. Looking up a new tag from anywhere other than back/forward truncates everything after the current position (standard browser-history behavior).

## Title block

Two stacked labels, centered, max width 800 px:

- **Title** - the wiki page's title (`title` field from the JSON), falling back to the looked-up tag name if the field is empty.
- **Aliases** - if the page has `other_names`, shows `Also known as: name1, name2, ...`. Hidden when there are no aliases.

Tag names are normalized for display (underscores -> spaces) so a character tag like `hatsune_miku` renders as `Hatsune Miku`.

## Body content

The wiki body is delivered as **DText** (Danbooru's markup format). The page converts it to HTML on the fly via `dtextToHtml`, walks the resulting HTML for `!post #N` and `!asset #N` references, and replaces each one with a `<img>` pointing at an internal resource URL (`post:N` / `asset:N`).

### Thumbnail embeds

Wiki pages on Danbooru commonly include `!post #1234567` (or asset references) to embed example images. Each one becomes a 150 x 150 thumbnail in the rendered body. The lifecycle:

1. **Layout placeholder** - a transparent 150 x 150 pixmap is registered at the resource URL immediately so the document layout is final from the first paint. No broken-image glyph flashing; no layout shifts when thumbs arrive.
2. **Paced metadata fetch** - one fetch per 250 ms (4 req/sec) for each thumb's metadata (`/posts/N.json` or `/media_assets/N.json`). This stays comfortably under Danbooru's per-IP rate limit even on image-heavy wiki pages (some character pages reference dozens of posts).
3. **Pixmap fetch + scale** - once the metadata returns the `preview_file_url` (post) or smallest acceptable variant (asset), the image is downloaded, scaled to 150 x 150 with `KeepAspectRatio`, and padded onto a transparent canvas so non-square thumbs don't stretch.
4. **Coalesced fade** - a single 25 ms timer drives the alpha for every in-flight thumb fade. So a 100-thumb gallery triggers one document re-layout per tick, not one hundred.
5. **Caching** - finished thumbs land in `m_postThumbs` / `m_assetThumbs` by id. Re-navigating to the same wiki page hits cache and skips the fetch entirely.

The 250 ms pacing and 25 ms fade tick are tuned for "responsive but not aggressive" - long wiki pages start showing thumbs within a second and finish populating within a few.

### Wiki link clicks

DText `[[other_tag]]` references render as anchors. Clicking one fires `onAnchorClicked` which:

- For an internal wiki-link anchor: triggers a page-level fade and looks up the linked tag (pushes onto history).
- For an HTTP URL: opens externally in the browser.
- For a `#fragment` (same-page anchor): smoothly scrolls to the anchor's position.

The smooth scroll fakes its way around `QTextBrowser`'s lack of position-from-anchor API: snap to the anchor to read the resulting scroll value, snap back, then animate to the read value. Snap+snap-back fires before any paint, so visually the page just glides there.

### Fade transitions

The whole `QStackedWidget` (loading + content + not-found) sits behind one shared `QGraphicsOpacityEffect`. A 180 ms `InOutSine` animation fades the page out before navigation kicks off (`startFadeOutThenLookup` / `startFadeOutThenHistory`), and fades back in once the new content lands in `displayContent` or `showNotFound`. The shared opacity means all three states - including transitions between content and not-found - cross-fade uniformly.

## States

The body area swaps between three states via the inner stack:

| State | When | What's shown |
| --- | --- | --- |
| **Loading** | After `showLoading()` fires (typically at the start of every `fetchWikiPage`). | Blank placeholder. The search bar is hint enough. |
| **Content** | After `displayContent` lands a valid JSON response. | Title block + rendered body. |
| **Not found** | After a `404` from the wiki API. | `No wiki page found for "<tag>".` |

A network error shows a "Network error: ..." message in the not-found slot with the error string.

## Caching

Wiki JSON responses are cached in-memory per session (`m_wikiCache[tag] = byteArray`). Looking up a previously-fetched tag is instantaneous; no second network round-trip until the app restarts.

Thumbnail pixmaps are also cached per session (`m_postThumbs`, `m_assetThumbs`). Switching between wiki pages that share embedded posts pulls thumbs from cache without re-fetching.

Neither cache is persisted to disk. The trade-off:

- **Pro**: simpler invariants, no stale-cache problems, no `data/wiki/` folder management.
- **Con**: a fresh launch re-fetches everything you visit.

## Keyboard

| Key | Effect |
| --- | --- |
| **Alt+Left** | Back. |
| **Alt+Right** | Forward. |
| **Enter** in the search bar | Look up the typed tag (after autocomplete commits it). |
| **Down** in an empty search bar | Standard `TagSearchBar` behavior - focuses the first suggestion. |
| Wheel | Smooth-scrolls the body. The viewport's wheel event is filtered to inject the animation, otherwise the scroll would snap. |

The body's `QTextBrowser` has `Qt::NoFocus` so keyboard events don't disappear into it - back/forward shortcuts work regardless of where focus is on the page.

## Coordination with other pages

Other pages route to the wiki via a `wikiRequested(tag)` signal handled at the `AppMainWindow` level. The handler:

1. Switches the main stack to `Page::DanbooruWiki`.
2. Calls `m_wikiPage->lookupTag(tag)` which kicks off the fetch (or loads from cache).

A "Wiki" action exists in every right-click menu the app surfaces for tag rows: [[Tag Composer]] tag rows, [[Tile View]] entry-panel tag rows, [[Facet Editor]] tag-list rows, and [[Dataset Helpers]] cluster-result rows.

## Tips

- **The search bar autocompletes against the danbooru tag index.** Same data file the rest of the app uses (`data/system/danbooru.csv`). If a tag isn't in your local index, it won't autocomplete, but you can still type the full name and Enter to look it up.
- **Back/Forward is per-session.** It doesn't survive an app restart. Use the external-open button to bookmark a page in your real browser if you want to come back to it later.
- **Image-heavy pages take a few seconds to fully populate.** The 250 ms pacing means a 60-thumb page finishes at about 15 seconds. Most don't have that many - typical character pages run 5-15 thumbs.
- **Pages that reference deleted Danbooru posts will leave transparent placeholders.** The metadata fetch returns 404, the placeholder stays. Not technically broken, just empty.
- **Click thumbnails in the body to open the source post on Danbooru.** The anchor wraps the `<img>` so the click-through still works after the fade-in lands.
- **The external-open button is your escape hatch for parser bugs.** If a wiki page renders weirdly in the in-app browser (unsupported DText extension, table layout quirk, etc.), `Open this page on danbooru.donmai.us` gives you the source-of-truth view in one click.
- **Long-press on a tag elsewhere isn't a thing.** Tag-row context menus are right-click only. On a trackpad / single-button mouse this is a two-finger tap or Ctrl-click.

## See also

- [[Tag Composer]] - right-click on a tag row -> Wiki.
- [[Tile View]] - right-click on a tag in the entry panel -> Wiki.
- [[Facet Editor]] - right-click on the selected-tag header or list rows -> Go to Wiki.
- [[Dataset Helpers]] - right-click on Tag Cluster result rows -> Wiki.
