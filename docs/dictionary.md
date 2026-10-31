# Dictionary

Look up words and phrases while reading an EPUB using an offline StarDict dictionary stored on the SD card.

## Supported Format

The reader supports **StarDict** dictionaries. When searching for dictionaries online, look for "StarDict format" or files with `.dict`, `.idx`, and `.ifo` extensions.

A dictionary folder must contain:

- `.idx` — word index (required, **must be uncompressed** — a `.idx.gz` will not work; decompress it on your computer with `gzip -d` first)
- `.dict` or `.dict.dz` — definition data (`.dict.dz` is supported as-is; entries are decompressed on the fly during lookup)
- `.syn` — synonym index (optional; maps alternate spellings and irregular forms to their headword)
- `.ifo` — metadata (optional; may carry a `lang=` language tag, see below)

Not supported: dictionaries with 64-bit index offsets (`idxoffsetbits=64` in the `.ifo` — rare, and rejected with an error).

> [!NOTE]  
> If you use .dict.dz and Crosspoint failed to load your dictionary with error message "Not enough memory", try unzipping it.

## Setting Up a Dictionary

1. Copy your dictionary folder(s) to `/dictionaries/` on the SD card — one dictionary per folder, e.g. `/dictionaries/webster/webster.idx` + `webster.dict.dz`. A hidden `/.dictionaries/` folder (dot-prefixed) works the same way, for keeping it out of the file browser.
2. Open **Settings → Reader → Dictionary** on the device.
3. Select a dictionary from the list, or **None** to disable lookups.

The Dictionary setting only appears when at least one usable dictionary folder exists. Folders containing more than one dictionary (multiple `.idx` stems) are skipped as ambiguous.

### Language-Specific Dictionaries

A dictionary can be marked as being for books in a given language by adding a `lang=` line to its `.ifo` file:

```
lang=fr
```

Every installed dictionary is still searched, but those whose tag matches the book's language (the EPUB's `dc:language`) are consulted and shown first. Matching compares the primary language subtag, ignoring case, so `lang=en` serves `en`, `en-US` and `en-GB` books, and a bilingual `lang=fr-en` dictionary serves French books. List several languages with commas (`lang=de,nl`).

Dictionaries without a `lang=` tag (or without an `.ifo`) count as matching every book, and so does every dictionary for a book without language metadata. The dictionary selected in **Settings → Reader → Dictionary** is the default: it leads the matching dictionaries when it serves the book's language, and leads the others when it does not.

## Looking Up Words and Phrases

Two ways to start a lookup while reading:

- Open the reader menu (**Confirm**) and choose **Look Up**.
- Or set **Settings → Controls → Long-press Menu** to "Dictionary", then hold **Confirm** (~0.4s) on the reading page.

One word on the page becomes highlighted:

1. Use **Left/Right** to move between words in reading order, and the side **Up/Down** buttons to jump between lines.
2. Press **Confirm** to look up the highlighted word.
3. Press **Back** to return to the reader.

To look up a phrase, choose **Create Clipping** from the reader menu, select the words, then choose **Look Up**.

On touch devices (X4 Pro and other touchscreen readers), long-press a word on the reading page to select it. A bubble above the word offers:

- **Look Up** — show its definition (only when a dictionary is selected). Tapping the selected word does the same.
- **Highlight** — start a highlight at this word. Tap further words to extend it, then tap **Done** (or the last word again) to save it. On a word that is already highlighted, this becomes **Remove Highlight**. Highlights are saved per book in `/.crosspoint/highlights/` and drawn with a gray background.
- **Go to** — follow the link the word belongs to (only on linked words).

Tap another word to move the selection there, or tap away from the text to return to the reader. While **Settings → Controls → Long-Press Word to Look Up** is on (the default), this replaces **Look Up** and **Highlight** in the reader menu. Turn it off to get those menu entries back and leave long-presses to page-turn behavior.

On the very first lookup with a dictionary (and again whenever the `.idx` or `.syn` source file changes), the reader shows *"Indexing dictionary…"* while it builds small sidecar files next to them — a `.qidx` for the word index, and a `.sidx` when a `.syn` synonym file is present. Each sidecar is rebuilt independently, only when its own source changes. This takes a few seconds for large dictionaries and makes all subsequent lookups fast. The sidecars can be deleted safely at any time — they will simply be rebuilt.

### How Lookup Works

1. **Direct match** — the word or phrase is searched as-is (case-insensitive) in the dictionary index. Leading and trailing punctuation and whitespace are stripped automatically. Multi-word phrases match dictionary entries containing spaces (such as idioms, phrasal verbs, or compound terms). When the dictionary has several entries for the same headword (common in dictionaries converted from Kindle/mobi files, which give each sense or part of speech its own entry — e.g. Spanish *perro* the adjective and *perro* the noun), all of them are shown, one after another, separated by a rule.
2. **Synonyms** — on a miss, if the dictionary ships a `.syn` file, alternate spellings and irregular forms recorded there are resolved to their headword (e.g. `oxen` → `ox`, `colour` → `color`). This step is skipped if the `.sidx` sidecar could not be built (e.g. transient low memory during indexing); the dictionary otherwise stays usable, and the build is retried the next time it is opened.
3. **Stemming** — still no match: common English word forms are retried automatically: possessives and plurals (`dogs` → `dog`, `stories` → `story`) and verb endings (`walked` → `walk`, `running` → `run`, `making` → `make`).
4. **Other dictionaries** — if the default dictionary still has no match, the other dictionaries are tried in turn (steps 1–3 each), those for the book's language first, and the first one with the word is shown.
5. **Not found** — no installed dictionary has the word: a short popup appears and you return to word selection.

## The Definition Screen

When a word is found, the definition screen shows the matched headword at the top and the definition text below, with a page counter for long definitions.

HTML dictionaries (`sametypesequence=h`) and XDXF dictionaries (`sametypesequence=x`) use the EPUB text-layout engine for supported formatting such as headings, bold, italics, lists, and line breaks. Unrecognized tags keep their text without special formatting. Images and CSS styling are ignored. Definitions that are too large or cannot be parsed or laid out within the available memory fall back to plain text.

Definitions are drawn in your reading font. Dictionaries may use IPA symbols (e.g. `ˈ ː ɹ ɪ ʊ`…) as pronunciation guide, which built-in reader fonts do not include, so they are shown as `�`. To see them, install an [SD card font](./sd-card-fonts.md) that covers IPA and select it as the reading font.

When more than one dictionary is installed, a row of tabs sits under the headword. The definition is shown first from your default dictionary, or otherwise the first dictionary with the word, preferring the book's language, and its tab is selected. While you read it, the word is looked up in the remaining dictionaries in the background (a `...` placeholder marks the search in progress), and a tab appears for each dictionary that has the word. Dictionaries without it get no tab. The results are already loaded, so switching tabs displays them immediately (only unusually long definitions, beyond what fits in memory alongside the others, are re-read when their tab is chosen); your default dictionary setting is unchanged. Tabs are labelled with the dictionary folder names, so short folder names read best.

- **Left/Right** or side **Up/Down** — previous / next page
- **Confirm** — switch to the next dictionary tab
- **Back** (or the header's back arrow on touch devices) — return to the book
- Touch: tap a tab to switch dictionaries; tap the left third of the screen for the previous page, elsewhere for the next


## Where to find dictionaries

> credit to https://github.com/koreader/koreader/wiki/Dictionary-support for the list.

- The [reader.dict](https://www.reader-dict.com) (ex "BoboTiG/ebook-reader-dict") project provides StarDict version of daily dumps of [Wiktionary](https://www.wiktionary.org/) monolingual dictionaries for a variety of languages. It also provides [non-free multilingual](https://www.reader-dict.com) dictionaries.
- The [WikDict](https://www.wikdict.com) project provides free bilingual dictionaries based on [Wiktionary](https://www.wiktionary.org/) for a lot of language pairs. StarDict versions can be [downloaded from here](https://download.wikdict.com/dictionaries/stardict/).
- The [`Vuizur/Wiktionary-Dictionaries`](https://github.com/Vuizur/Wiktionary-Dictionaries) repository contains dictionaries based on [Wiktionary](https://www.wiktionary.org/) from many languages to English, including English-English.
- The [DictInfo](https://www.dictinfo.com/) website provides outdated monolingual dictionaries based on [Wiktionary](https://www.wiktionary.org/).
- The [Firedict site](https://tuxor1337.frama.io/firedict/dictionaries.html) contains a list of freely available dictionaries.
- [wiktionary_stardict](https://xxyzz.github.io/wiktionary_stardict/): update monthly.
- [Fictionaries](https://fictionary.gumroad.com/) provides dictionaries for various speculative fiction books and series.
- [World Factbooks Archive](https://github.com/MilkMp/CIA-World-Factbooks-Archive-1990-2025) provides 36 years of CIA's World Factbook dictionaries in StarDict format.
- [StarDict-Hebrew](https://github.com/Uri-Tauber/StarDict-Hebrew) Hebrew-English StarDict versions of Babylon dictionaries.
