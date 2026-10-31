# nexlibrOS User Guide

Welcome to **nexlibrOS**, a personal fork of the [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) firmware tuned for the touchscreen Xteink X4 Pro. This guide outlines the hardware controls, navigation, and reading features of the device.

nexlibrOS behaves like CrossPoint except for the following changes:

- **[nexlibrOS home screen](#31-home-screen)** (X4 Pro, default): cards for your recent books with reading progress, and an icon bar for Settings, File Transfer, Apps, and the Library.
- **[Apps](#39-apps)**: small Lua programs on the SD card, including a calculator, calendar, unit converter, crosswords, dictionary, flashcards, photo gallery, notepad, and offline Wikipedia.
- **[Library lists](#34-library-screen)**: Series, Tags, Folders, and custom lists alongside Recent, Title, and Author, with sorting by the title and author sort fields stored in each book.
- **[Reorganized Settings](#36-settings)**: one screen of seven sections instead of four tabs.
- **[Cloud Libraries](#35-file-transfer-screen)**: the OPDS browser has moved from the Home screen to File Transfer. Downloads prefer X4/X3-optimized EPUBs.
- **[Long-press a word](#dictionary-lookup)** (touch) to look it up, [highlight](#highlights) it, or follow its link. If you have several dictionaries installed, each one gets a tab.
- **[Bookmarked pages](#52-bookmarks)** show a mark in the top-right corner. On touch devices, tap the corner to add or remove the bookmark.

- [nexlibrOS User Guide](#nexlibros-user-guide)
  - [1. Hardware Overview](#1-hardware-overview)
    - [Controls](#controls)
    - [Touch Gestures](#touch-gestures)
    - [Taking a Screenshot](#taking-a-screenshot)
    - [Frontlight (X4 Pro and Paper Mono)](#frontlight-x4-pro-and-paper-mono)
  - [2. Power \& Startup](#2-power--startup)
    - [Power On / Off](#power-on--off)
    - [First Launch](#first-launch)
  - [3. Screens](#3-screens)
    - [3.1 Home Screen](#31-home-screen)
    - [3.2 Reading Mode](#32-reading-mode)
    - [3.3 Browse Files Screen](#33-browse-files-screen)
    - [3.4 Library Screen](#34-library-screen)
    - [3.5 File Transfer Screen](#35-file-transfer-screen)
    - [3.5.1 Calibre Wireless Transfers](#351-calibre-wireless-transfers)
      - [Installing the Plugin in Calibre](#installing-the-plugin-in-calibre)
      - [Configuring the CrossPoint Plugin in Calibre](#configuring-the-crosspoint-plugin-in-calibre)
      - [Uploading Books](#uploading-books)
      - [Removing a Book](#removing-a-book)
    - [3.6 Settings](#36-settings)
      - [3.6.1 General](#361-general)
      - [3.6.2 Display](#362-display)
      - [3.6.3 Reader](#363-reader)
      - [3.6.4 Controls](#364-controls)
      - [3.6.5 Library](#365-library)
      - [3.6.6 Network](#366-network)
      - [3.6.7 System](#367-system)
      - [3.6.8 OPDS Servers (Multiple Libraries)](#368-opds-servers-multiple-libraries)
      - [3.6.9 Web Settings (Wi-Fi + OPDS)](#369-web-settings-wi-fi--opds)
      - [3.6.10 KOReader Sync Quick Setup](#3610-koreader-sync-quick-setup)
        - [Option A: CrossPoint Sync Server (`sync.crosspointreader.com`, default)](#option-a-crosspoint-sync-server-synccrosspointreadercom-default)
        - [Option B: Legacy Public KOReader Server (`sync.koreader.rocks`)](#option-b-legacy-public-koreader-server-synckoreaderrocks)
        - [Option C: Self-Hosted Server (Docker Compose)](#option-c-self-hosted-server-docker-compose)
        - [Syncing While Reading](#syncing-while-reading)
    - [3.7 Sleep Screen](#37-sleep-screen)
      - [Cover settings](#cover-settings)
      - [Custom images](#custom-images)
    - [3.8 Custom Fonts (SD Card)](#38-custom-fonts-sd-card)
    - [3.9 Apps](#39-apps)
  - [4. Reading Mode](#4-reading-mode)
    - [Page Turning](#page-turning)
    - [Chapter Navigation](#chapter-navigation)
    - [Auto Page Turn](#auto-page-turn)
    - [Tilt Page Turn (X3 only)](#tilt-page-turn-x3-only)
    - [Links and footnotes](#links-and-footnotes)
    - [Dictionary Lookup](#dictionary-lookup)
    - [Highlights](#highlights)
    - [System Navigation](#system-navigation)
    - [Supported Languages](#supported-languages)
  - [5. Reader Menu](#5-reader-menu)
    - [5.1 Chapter Selection](#51-chapter-selection)
    - [5.2 Bookmarks](#52-bookmarks)
    - [5.3 Highlights](#53-highlights)
  - [6. Current Limitations \& Roadmap](#6-current-limitations--roadmap)
  - [7. Troubleshooting Issues \& Escaping Bootloop](#7-troubleshooting-issues--escaping-bootloop)

## 1. Hardware Overview

This guide is written for the **Xteink X4 Pro**, which is used mainly through its touchscreen. Notes marked **On button-only devices** cover the Xteink X4 and X3, which are driven entirely by physical buttons.

### Controls

The X4 Pro has a touchscreen and four hardware controls:

| Control | What it does by default |
| --- | --- |
| **Touchscreen** | Tap to open or select, long-press for options, swipe to scroll or turn pages (see [Touch Gestures](#touch-gestures)). |
| **Home key** (capacitive) | Tap: go to the **[Home](#31-home-screen)** screen. Double tap: turn the frontlight on or off. Long press: open the **[Reader Menu](#5-reader-menu)** while reading. |
| **Side Up / Side Down** | Previous / next page while reading; move up / down through lists elsewhere. |
| **Power** | Hold to turn the device on or off. Double-click to turn the frontlight on or off. |

The Home key's tap, double-tap, and long-press actions can each be changed in **Settings → Controls → Home Button Gestures** (see [Controls Settings](#364-controls)).

### Touch Gestures

These gestures work on almost every screen:

| Gesture | Action |
| --- | --- |
| **Tap** | Open or select the item under your finger. |
| **Long-press** | Show options for an item, such as a book in the Library or a file in Browse Files. While reading, long-press a word to [look it up or highlight it](#dictionary-lookup). |
| **Swipe up / down** | Scroll a list. |
| **Swipe right from the left edge**, or tap the **back arrow** in the header | Go back one screen. *(Not used on the reading page itself, where a right swipe turns back a page.)* |
| **Swipe down from the top edge** | Open the [frontlight panel](#frontlight-x4-pro-and-paper-mono). |

See [Reading Mode](#4-reading-mode) for the gestures used on the reading page.

#### On button-only devices

The X4 and X3 use the buttons in the same layout as the manufacturer firmware, by default:

| Location        | Buttons                                              |
| --------------- | ---------------------------------------------------- |
| **Bottom Edge** | **Back**, **Confirm**, **Left**, **Right**           |
| **Right Side**  | **Power**, **Side Up**, **Side Down**, **Reset** |

**Back** goes back, **Confirm** opens or selects, and **Left/Right** (or **Side Up/Side Down**) move through lists. Holding **Confirm** usually shows an item's options, matching a long-press on touch devices. The front buttons can be remapped in the **[Controls Settings](#364-controls)**.

### Taking a Screenshot

Press **Power** and **Side Down** at the same time to take a screenshot. It is saved in the `screenshots/` folder.

Alternatively, while reading a book, open the **[Reader Menu](#5-reader-menu)** and select **Take screenshot**.

### Frontlight (X4 Pro and Paper Mono)

The X4 Pro and the M5Stack Paper Mono have a built-in frontlight. The X4 Pro has adjustable brightness and warmth; the Paper Mono has adjustable brightness only. It is controlled from a swipe panel rather than the Settings menu:

* **Open the frontlight panel:** Swipe down from the top edge of the screen, from almost any screen (Home, Browse Files, Reading Mode, etc.). Drag the brightness slider (and the warmth slider, on the X4 Pro) to adjust the light live, or tap the sun icon to turn it on or off.
* **Quick toggle (X4 Pro only):** Double-tap the **Home** key or double-click the **Power** button to turn the frontlight on or off instantly, without opening the panel. The Power shortcut can be turned off with **Double-Click Power for Light** in [Controls Settings](#364-controls).

> [!NOTE]
> Frontlight brightness and warmth are intentionally not listed in **[Display Settings](#362-display)** — the swipe panel is the only place to adjust them. On the X4 Pro, the on/off state can also be toggled with the Home-key double tap or Power-button double-click above.

If the frontlight doesn't come back on after the device wakes from sleep, check **Restore Light on Wake** in **[Display Settings](#362-display)** (on by default). Turning it off is intentional if you'd rather have the light stay off on wake and switch it on yourself each time — but it's easy to forget you changed it.

---

## 2. Power & Startup

### Power On / Off

To turn the device on or off, **press and hold the Power button for approximately half a second**.
In the **[Controls Settings](#364-controls)** you can configure the power button to turn the device off with a short press instead of a long one.

To reboot the device (for example after a firmware update or if it's frozen), press and release the Reset button, and then quickly press and hold the Power button for a few seconds.

### First Launch

Upon turning the device on for the first time, you will be placed on the **[Home](#31-home-screen)** screen. On the X4 Pro, this is the touch-first "nexlibrOS" home screen this guide assumes (see [nexlibrOS theme](#nexlibros-theme)).

> [!NOTE]
> On subsequent restarts, the firmware will automatically reopen the last book you were reading.

---

## 3. Screens

### 3.1 Home Screen

The Home screen is the main entry point to the firmware. Tap the **Home** key from anywhere to return to it.

#### nexlibrOS theme

"nexlibrOS" is the X4 Pro's touch-first home screen and its default theme. It is set in **Settings → Display → UI Theme** (it is only offered on the X4 Pro). It shows:

- **Book cards:** a large card for the book you read most recently, followed by smaller cards for the next most recent books, as many as fit on the screen. Each card shows the book's cover, title, and author, plus a ring that fills in as you read and shows a check mark once you finish the book.
- **Icon bar:** four icons along the bottom, from left to right: **Settings**, **File Transfer**, **[Apps](#39-apps)**, and **[Library](#34-library-screen)**.

Tap a card to continue that book, or tap an icon to open it. The side keys move a selection outline if you prefer buttons.

This theme has no Browse Files icon. To browse your books by folder, turn on the Library's **Folders** list (see [Library Settings](#365-library)).

#### Other themes

The other themes show the most recent book (or several, depending on the theme) and a menu of **[Browse Files](#33-browse-files-screen)**, the **[Library](#34-library-screen)**, **[File Transfer](#35-file-transfer-screen)**, and **[Settings](#36-settings)**. A **Plugins** entry also appears when plugins are enabled (**Settings → System → Plugins**) or any [app](#39-apps) is installed. Tap an entry to open it, or on button-only devices, move through the menu with **Left/Right** and press **Confirm**.

The OPDS browser is no longer on the Home screen. Open it from **File Transfer → Cloud Libraries** (see [File Transfer](#35-file-transfer-screen)).

### 3.2 Reading Mode

See [Reading Mode](#4-reading-mode) below for more information.

### 3.3 Browse Files Screen

The Browse Files screen acts as a file and folder browser. The full path to the current directory is shown at the top of the screen. File extensions are displayed alongside each filename, and directories are shown with brackets (e.g. `[folder-name]`). Hidden directories (those beginning with `.`) are also visible.

* **Scroll:** Swipe up or down through folders and books.
* **Open:** Tap a folder to open it, or tap a book to start reading. Tapping a `.bmp` file opens the image viewer.
* **File options:** Long-press a book or file for **Open**, **Delete**, and **Rename**. Deleting asks you to confirm or cancel.
* **Go up a folder:** Swipe right from the left edge, or tap the back arrow in the header.

**On button-only devices:** Use **Left** (or **Side Up**) and **Right** (or **Side Down**) to move the selection, and long-press them to scroll a full page. Press **Confirm** to open the selection, and hold and release **Confirm** for the file options. Press **Back** to go up a folder.

### 3.4 Library Screen

The Library indexes up to 4,096 supported books on the SD card and shows their titles and authors without requiring you to remember their folders. It offers several lists, each a different view of your books. An arrow beside the Title or Author tab shows the sort direction:

- **Recent** lists the books you opened most recently first, followed by the rest of the library by modification date. Long-press a book for options: **Open**, **Remove from Recents**, **Delete**, or **Refresh library**.
- **Title** groups books by the first letter of the title. Up sorts A-Z and down sorts Z-A. Titles beginning with numbers or punctuation appear under `#`; letters from non-English scripts, including Hebrew, have their own groups. When a book provides a title sort (for example "Hobbit, The"), it is sorted by that.
- **Author** groups books by author. Up sorts A-Z and down sorts Z-A. Authors are ordered by the author sort stored in the book when there is one, otherwise by surname. "Victor Hugo" and "Hugo, Victor" count as the same author.
- **Series** has one list per series, with its books in series order.
- **Tags** has one list per tag (the book's subjects or genres), up to the 128 most used tags. Spellings that differ only in case or accents count as one tag.
- **Folders** mirrors the SD card's folders: each folder lists its subfolders, then its books.
- **Custom** lists are defined by you in `/.crosspoint/customlists.json` (see below).

Recent, Title, and Author are shown by default. Choose which lists appear in **Settings → Library → Library** (see [Library Settings](#365-library)). Up to four lists appear as tabs. With more than four, the Library opens on a picker of all of them. Series, Tags, and Custom lists come from book metadata, so they are empty when **Use Book Metadata** is off.

#### Custom lists

Create `/.crosspoint/customlists.json` on the SD card to build lists from your books' tags. Each entry is a list. Its value is either a single tag, which makes a list of the books with that tag, or a set of named sublists, each matching one tag:

```json
{
  "By genre": {
    "Nonfiction": "nonfiction",
    "Fiction": "fiction"
  },
  "Favorites": "favorite"
}
```

Tags match regardless of case and accents. Up to 16 lists and 256 sublists are read. Turn on **Custom (customlists.json)** in the Library settings to show them. The Library rebuilds itself when the file changes. See [docs/file-formats.md](docs/file-formats.md) for details.

#### Navigating the Library

- **Open:** Tap a book to open it, or tap a series, tag, folder, or custom list to open that list.
- **Switch lists:** Tap a tab. Tap the active Title or Author tab again to reverse its sort direction. When the Library uses the list picker, tap a list in the picker, and go back to return to it.
- **Scroll:** Swipe up or down.
- **Search:** Tap the Search icon.
- **Options:** Long-press a book in the Recent view for its options menu.
- **Groups:** In a Title or Author view, long-press a book to collapse the list to its letter or author groups, then tap a group to expand it.
- **Go back:** Swipe right from the left edge, or tap the back arrow in the header, to return to the list above.

**On button-only devices:**

- Use **Up/Down** or **Left/Right** to move one row at a time. Hold a direction to move a page at a time.
- Press **Confirm** to open the selected book.
- Press **Back** from the book list to focus the tabs. Use **Left/Right** to select another tab, press **Confirm** to reverse its sort direction, or press **Down** to return to the list.
- While the tabs are focused, hold **Confirm** to open Search.
- In the Title or Author views, hold **Confirm** on a book to collapse the list to its letter or author groups. The matching group remains selected. Press **Confirm** to enter a group, or **Back** to restore the exact book and position you came from.
- In lists of series, tags, folders, or custom sublists, press **Confirm** to open the selected entry and **Back** to return to the list above it. When the Library uses the list picker, **Back** from a top-level list returns to the picker.

The index is created automatically the first time the Library is opened, and rebuilt when you change which lists are shown. To pick up later file changes or updated metadata, long-press a book in the Recent view and choose **Refresh library**. The **Use Book Metadata** setting controls whether the index reads the titles, authors, sort fields, series, and tags stored inside books.

### 3.5 File Transfer Screen

The File Transfer screen allows you to upload and manage files on the device. When you enter the screen, choose **Join a Network**, **Calibre Wireless**, **Create Hotspot**, or **Cloud Libraries** (on the X4 Pro, **USB Drive** is also offered). The first three start the web server for the selected mode.

**Cloud Libraries** opens the OPDS browser, which lets you browse, search, and download from the servers configured in **[OPDS Servers](#368-opds-servers-multiple-libraries)**. When a catalog offers several EPUB downloads for one book, nexlibrOS picks an `.x4.epub` or `.x3.epub` file (an EPUB optimized for these devices, such as one made with the Calibre plugin) over a plain `.epub`.

See the [web server docs](./docs/webserver.md) for more information on how to connect to the web server and upload files.

The web interface also supports **WebDAV**, allowing you to mount the device as a network drive and manage files directly from your computer's file manager.

Download links for files already on the device are available in the web interface, so you can retrieve books or screenshots over Wi-Fi without connecting a cable.

A **Wi-Fi signal strength indicator** (dBm) is displayed on-screen during joined-network web server sessions.

> [!TIP]
> Advanced users can also manage files programmatically or via the command line using `curl`. See the [web server docs](./docs/webserver.md) for details.
> [!TIP]
> If your EPUBs have compatibility issues, you can run the built-in **EPUB Optimizer** directly from the device to clean up and reprocess books for better rendering.

### 3.5.1 Calibre Wireless Transfers

CrossPoint supports sending books from Calibre using the CrossPoint Reader device plugin.

#### Installing the Plugin in Calibre

If you don't already have the plugin installed:

1. Head to https://github.com/crosspoint-reader/calibre-plugins/releases to download the latest version of the crosspoint_reader plugin.
2. Download the zip file.
3. Open Calibre → Preferences → Plugins → Load plugin from file → Select the zip file.
4. Restart Calibre.

#### Configuring the CrossPoint Plugin in Calibre
1. In Calibre select Preferences.
2. In the Preferences dialog select Plugins.
3. In Plugins search for "crosspoint".
4. Click on "Customize plugin".
5. Update the value for "Host" to match the IP for your device.
6. Leave the other settings as they are.
7. [optional] Modify the "Upload path" to point to a subfolder other than the root "/" folder. Enter this as a path relative to the root folder. Example: `/mybooks`
8. Restart Calibre.

<img width="420" height="385" alt="Image" src="https://github.com/user-attachments/assets/01fc7e33-a9a7-48ba-9e26-2e68d1f9daec" />

#### Uploading Books

To upload a book using the CrossPoint plugin in Calibre:

1. On the device: File Transfer -> Calibre Wireless, then join a network.
2. Select one or more books.
3. Right-click on that selection.
4. Select "Send to Device" > "Send to main memory"

The CrossPoint plugin will connect to your device, create a folder for the book's author in the root folder (or the folder you configured for the plugin), then copy the book into that folder.

<img width="783" height="310" alt="Image" src="https://github.com/user-attachments/assets/741b0909-2e1d-4f16-8af0-2c43fbda5ce6" />

#### Removing a Book

Books cannot be removed from your device through Calibre. Use the web interface instead.

### 3.6 Settings

The Settings screen allows you to configure the device's behavior. It opens on a list of seven sections: **General**, **Display**, **Reader**, **Controls**, **Library**, **Network**, and **System**. Under each section's name is either the names of its settings or a short summary. Tap a section to see its settings, and go back (swipe right from the left edge, or tap the back arrow) to return to the list. The firmware version is shown in the header.

Tap a setting to change it: on/off settings toggle, settings with two choices switch between them, and settings with more choices open a list to pick from. Some rows open their own screen.

Some settings only appear on devices that support them, such as touch controls, the Home key, the frontlight, or the clock. On button-only devices, move through settings with **Left/Right**, press **Confirm** to change one, and press **Back** to return.

#### 3.6.1 General

- **Language**: Set the UI language. CrossPoint supports 32 languages: English, Spanish, French, German, Czech, Brazilian Portuguese, European Portuguese, Russian, Swedish, Romanian, Catalan, Ukrainian, Belarusian, Italian, Polish, Finnish, Danish, Dutch, Turkish, Kazakh, Hungarian, Lithuanian, Slovenian, Valencian, Hebrew, Arabic, Slovak, Bosnian, Vietnamese, Norwegian Bokmål, Indonesian, and Orangutan. Language is listed first so it is easy to find if the UI is set to a language you can't read.

- **Keyboard Layouts**: Choose the layouts available on the on-screen keyboard.

- **Time to Sleep**: Set the duration of inactivity before the device automatically goes to sleep; options are 1, 3, 5, 10 (default), 15 or 30 minutes.

- **Clock**: Set the time and clock options. *(Only shown on devices with a real-time clock.)*

#### 3.6.2 Display

- **Sleep Screen**: Which sleep screen to display when the device sleeps:
  
  - "Dark" - The logo sleep screen, on a black background
  - "Light" (default) - The logo sleep screen, on a white background
  - "Custom" - Custom images from the SD card; see [Sleep Screen](#37-sleep-screen) below for more information
  - "Cover" - The book cover image (Note: this is experimental and may not work as expected)
  - "None" - A blank screen
  - "Cover + Custom" - The book cover image while actively reading, falls back to "Custom" behavior otherwise
  - "Quick resume" - The text of the last page read will be displayed on the sleep screen and a moon icon is shown on the edge of the screen. Waking up the device will return to the same page of the opened book. This is useful for quickly resuming reading without waiting for the device to fully wake up and load the book.
  - "Transparent" - A transparent overlay image drawn over the current screen; see [Sleep Screen](#37-sleep-screen) below for more information
- **Sleep Screen Cover Mode**: How to display the book cover when "Cover" sleep screen is selected:
  
  - "Fit" (default) - Scale the image down to fit centered on the screen, padding with white borders as necessary
  - "Crop" - Scale the image down and crop as necessary to try to fill the screen (Note: this is experimental and may not work as expected)

- **Sleep Screen Cover Filter**: What filter will be applied to the book cover when "Cover" sleep screen is selected:
  
  - "None" (default) - The cover image will be converted to a grayscale image and displayed as it is
  - "Contrast" - The image will be displayed as a black & white image without grayscale conversion
  - "Inverted" - The image will be inverted as in white & black and will be displayed without grayscale conversion

- **Quick Resume on Timeout**: Whether to enable the "Quick Resume" sleep screen when the device goes to sleep due to inactivity (General > Time to Sleep). This is useful for quickly resuming reading without waiting for the device to fully wake up and load the book. This overwrites the Sleep Screen Cover Mode when enabled.

- **Hide Battery %**: Configure where to suppress the battery percentage display in the status bar; the battery icon will still be shown:
  
  - "Never" (default) - Always show battery percentage
  - "In Reader" - Show battery percentage everywhere except in reading mode
  - "Always" - Always hide battery percentage

- **Refresh Frequency**: Set how often the screen does a full refresh while reading to reduce ghosting; options are every 1, 5, 10, 15, or 30 pages.

- **UI Theme**: Set which UI theme to use:
  
  - "Classic" - The original Crosspoint theme
  - "Lyra" - The new theme for Crosspoint featuring rounded elements and menu icons
  - "Lyra Extended" - Lyra, but displays 3 books instead of 1 on the **[Home Screen](#31-home-screen)**
  - "RoundedRaff" - A rounded theme with additional visual styling
  - "Cover Grid" - A grid of book covers on the **[Home Screen](#31-home-screen)** *(only on devices with external RAM)*
  - "nexlibrOS" - Recent-book cards with progress rings and an icon bar on the **[Home Screen](#31-home-screen)** *(X4 Pro only; the default there)*

- **Sunlight Fading Fix**: Configure whether to enable a software-fix for the issue where white X4 models may fade when used in direct sunlight *(not shown on the X4 Pro or X4 Classic)*:
  
  - "OFF" (default) - Disable the fix
  - "ON" - Enable the fix

- **Restore Light on Wake**: Restore the frontlight's on/off state when the device boots or wakes (on by default). Brightness and warmth are always remembered. *(Frontlight devices only.)*

- **Night Mode**: Invert the screen to show light text on a dark background.

#### 3.6.3 Reader

- **Text Settings**: A sub-screen that collects the settings controlling how book text is laid out: **Reader Font Family**, **Reader Font Size**, **Line Spacing**, **Word spacing**, **Character spacing**, **Screen Margin**, **Paragraph Alignment**, **Embedded Style**, **Focus Reading**, **Hyphenation**, **Extra Paragraph Spacing**, **Paragraph Indentation**, and **Text Anti-Aliasing**. Several of these are described below.

- **Manage Fonts**: Browse, download, and manage custom font families installed from the SD card. See [Custom Fonts (SD Card)](#38-custom-fonts-sd-card) for more information.

- **Customise Status Bar**: Choose what the status bar shows while reading: chapter page count, book percentage, a book or chapter progress bar and its thickness, the book or chapter title, the battery, and the clock.

- **Reader Font Family**: Choose the font used for reading:
  
  - "Noto Sans" (default) - Google's sans-serif font
  - Installed SD card families

- **Reader Font Size**: Choose a point size. Built-in and direct TTF/OTF/TTC fonts offer 12, 14, 16, and 18 pt. A `.cpfont` family offers the sizes installed for that family.

- **Reader Line Spacing**: Adjust the spacing between lines; options are "Tight", "Normal" (default), or "Wide".

- **Reader Screen Margin**: Controls the screen margins in Reading Mode between 5 and 40 pixels in 5-pixel increments.

- **Reader Paragraph Alignment**: Set the alignment of paragraphs; options are "Justified" (default), "Left", "Center", or "Right".

- **Embedded Style**: Whether to use the EPUB file's embedded HTML and CSS stylisation and formatting; options are "ON" or "OFF".

- **Hyphenation**: Whether to hyphenate text in Reading Mode; options are "ON" or "OFF". Korean text wraps only at spaces when "OFF"; when "ON", a Korean word may also wrap at the end of a line between syllables or where it meets digits, Latin letters, or brackets (no hyphen is drawn).

- **Reading Orientation**: Set the screen orientation for reading EPUB files:
  
  - "Portrait" (default) - Standard portrait orientation
  - "Landscape CW" - Landscape, rotated clockwise
  - "Inverted" - Portrait, upside down
  - "Landscape CCW" - Landscape, rotated counter-clockwise

- **Extra Paragraph Spacing**: Set how to handle paragraph breaks:
  
  - "ON" - Vertical space will be added between paragraphs in Reading Mode
  - "OFF" - Paragraphs will not have vertical space added, but will have first-line indentation

- **Dictionary**: Select the StarDict dictionary used for word lookups while reading, or "None" to disable lookups. *(Only shown when at least one dictionary folder exists under `/dictionaries/` on the SD card — see [docs/dictionary.md](docs/dictionary.md) for setup and usage.)*

- **Text Anti-Aliasing**: Whether to show smooth grey edges (anti-aliasing) on text in reading mode. Note this slows down page turns slightly.

- **Images**: Whether to display embedded images (JPG/PNG/GIF) found in EPUB files; options are "ON" (default) or "OFF". GIFs render only the first frame; interlaced GIFs are not supported.

- **Focus Reading**: Bolds the first part of each word to create visual fixation points, similar to Bionic Reading. This can help improve reading speed and focus; options are "ON" or "OFF" (default).

#### 3.6.4 Controls

- **Home Button Gestures**: Choose what the X4 Pro's Home key does on a **Tap** (default "Home"), **Double Tap** (default "Toggle Frontlight"), and **Long Press** (default "Reader Menu"). Other choices include "Ignore", "Page Turn", "Refresh Screen", "Links and Footnotes", "Confirm", "KOSync", "Bookmark", "Dictionary", and "Highlight". *(Devices with a Home key.)*

- **Touch Reader Controls**: Turn touch page-turning on the reading page on or off. The frontlight panel also has a tile for toggling this quickly. *(Touch devices.)*

- **Long-Press Word to Look Up**: Long-press a word while reading to select it and get **Look Up**, **Highlight**, and **Go to** options (see [Dictionary Lookup](#dictionary-lookup) and [Highlights](#highlights)). On by default. While it is on, **Look Up** and **Highlight** are left out of the Reader Menu. *(Touch devices.)*

- **Next Page** / **Previous Page**: Which touch gestures turn the page in each direction (see [Page Turning](#page-turning)):
  
  - "Swipe Only" (default) - Swipe left for the next page, right for the previous page
  - "Tap & Swipe" - Swiping or tapping the side of the page both work
  - "Tap Only" - Tap the side of the page
  - "Inverted Tap" - Tap zones with the sides swapped
  - "Disabled" - No touch gesture turns the page in that direction
  
  *(Touch devices.)*

- **Show Reader Menu**: How to open the Reader Menu by touch: "Tap" (default, tap the middle of the page), "Swipe Up" (swipe up from the bottom edge), or "Off" (use the Home key's long press only). *(Devices with a Home key.)*

- **Double-Click Power for Light**: Double-click **Power** to turn the frontlight on or off (on by default). *(X4 Pro.)*

- **Short Power Button Click**: Controls the effect of a short click of the power button:
  
  - "Ignore" (default) - Require a long press to turn off the device
  - "Sleep" - A short press puts the device into sleep mode
  - "Page Turn" - A short press in reading mode turns to the next page; a long press turns the device off
  - "Refresh" - A short press triggers a manual full-screen refresh, useful for clearing ghosting
  - "Links and Footnotes" - A short press in reading mode opens link selection. If the page has only one link, it opens directly. After following a link, a short press returns to the previous location.
  - "Confirm" - A short press acts as **Confirm** *(touch devices only)*
  - "Highlight" - A short press in reading mode starts highlight selection *(button-only devices)*
  - "Bookmark", "KOSync", "Reader Menu" - A short press runs that action *(touch devices without a Home key)*
- **Quick-return from footnotes**: When **Short Power Button Click** is "Links and Footnotes", a short press of the power button acts as Back after following a link or footnote.

- **Side Button Layout (reader)**: Swap the order of the up and down side buttons from "Prev/Next" (default) to "Next/Prev", make both turn the same way, or disable them. This change is only in effect when reading.

- **Long-Press Button Behavior**: What holding a page-turn button does while reading: "Off" (default), "Chapter Skip" (skip to the next/previous chapter), or "Orientation Change".

The following apply **only to button-only devices**:

- **Remap Front Buttons**: A menu for customising the function of each bottom edge button.

- **Bluetooth Page Turner** (**Settings → Network**): Enable Bluetooth, then open **Scan for Remotes** to connect a page turner. **Paired Remotes** lists saved remotes; selecting a row marked **Forget** removes it. Once connected, open **Map Buttons**, choose a page, chapter, Confirm, Back, reader menu, or highlight action, and press the remote button within 15 seconds. Release the button within 15 seconds: a hold of at least 0.7 seconds is mapped separately from its tap. If the remote never reports a release, learning waits 15 seconds and saves the press as a tap. Hold Confirm on an action, or long-press its row, to clear it. Back cancels learning or returns to the Bluetooth menu. Mapping is optional: without custom mappings, the default remote controls remain available. Each button gesture maps to one action; assigning it again replaces its previous action. Confirm and Back act like the device buttons while reading and in the reader list menu. Next/Previous Page navigate items while a reader menu is open and turn pages when it is closed. Bluetooth can also be toggled from the reader list menu or the toolbar’s **More** menu.


- **Long-press Menu**: Selects the function bound to holding the menu button (Confirm) while reading an EPUB. **Cycles through the available functions** each time the setting is selected. A short press of Confirm always opens the reader menu as normal:
  - "Bookmark" (default) - Hold Confirm (~0.4 second) to drop a bookmark at the current page.
  - "KOSync" - Hold Confirm (~1 second) to launch KOReader sync directly.
  - "Dictionary" - Hold Confirm (~0.4 second) to start dictionary word selection on the current page (see [docs/dictionary.md](docs/dictionary.md)).
  - "Highlight" - Hold Confirm (~0.4 second) to start highlight selection on the current page.
  - "Links and Footnotes" - Hold Confirm to open link selection, or to return after following a link.
  - "Disabled" - Long-press is ignored; only short-press opens the reader menu.

- **Tilt Page Turn**: Turn pages by tilting the device *(X3 only)*.

#### 3.6.5 Library

- **Show Hidden Files**: Show files and folders whose names begin with `.` in **[Browse Files](#33-browse-files-screen)**.

- **Clear Read Books from Recent List**: Remove a book from the Recent list when you reach its end-of-book screen.

- **Move Finished Books to Read Folder**: Move a finished EPUB to a `/Read/` folder on the SD card.

- **Library**: A sub-screen for the **[Library](#34-library-screen)**:
  
  - **Use Book Metadata**: Read the title, author, sort fields, series, and tags stored inside each book when the Library index is rebuilt. When disabled or unavailable, the Library uses the filename, and the Series, Tags, and Custom lists are empty.
  - **Lists**: Turn each list on or off: **Recent**, **Title**, **Author**, **Series**, **Tags**, **Folders (file system)**, and **Custom (customlists.json)**. Recent, Title, and Author are on by default. At least one list must stay on. The index is rebuilt with the new lists the next time the Library opens.

- **Clear Reading Cache**: Clear the internal SD card cache.

#### 3.6.6 Network

- **Wi-Fi Networks**: Connect to Wi-Fi networks for file transfers and firmware updates.

- **KOReader Sync**: Options for setting up KOReader for syncing book progress. **Smart sync** is the default for new configurations and auto-resolves simple push/pull decisions. Existing credential files retain **Ask every time** when migrated; you can switch Sync Behavior at any time if you prefer manual confirmation. See [KOReader Sync Quick Setup](#3610-koreader-sync-quick-setup).

- **OPDS Servers**: Manage one or more OPDS [(Open Publication Distribution System)](https://en.wikipedia.org/wiki/Open_Publication_Distribution_System) libraries for browsing and downloading books. See [OPDS Servers (Multiple Libraries)](#368-opds-servers-multiple-libraries) below.

#### 3.6.7 System

- **Check for Updates**: Check for firmware updates over Wi-Fi.

> [!WARNING]
> **Check for Updates** downloads the latest **upstream CrossPoint** release, not nexlibrOS. Installing it replaces nexlibrOS and its features. To update nexlibrOS, build it yourself and use **SD Card Firmware Update** or flash it over USB.

- **SD Card Firmware Update**: Install firmware from a `firmware.bin` file on the SD card, without a USB connection.

> Neither update option is available on the BOOX Picco, which is updated over USB.

- **Plugins**: Enable or disable SD-card plugins. Clean installs start disabled; upgrades with existing plugins are kept enabled automatically. Disabling plugins leaves all plugin files on the SD card but stops plugin discovery and execution until they are enabled again.
  - Enabling plugins immediately adds **Plugins** to the Home screen, even when no plugins are installed yet. Sideloaded SD-card plugins work without Plugin Hub.
  - Open **Home → Plugins** to view installed plugins. If no usable on-device Plugin Hub is available and its installer has not been hidden, a **Plugin Hub — Download, discover, and update plugins** entry is also shown. A manually sideloaded Hub with a valid `device.json` is recognized without requiring the optional README or browser files.
  - Selecting that Plugin Hub entry offers **Install**, **Hide**, or **Cancel**. **Install** downloads Plugin Hub over Wi-Fi. If you back out of Wi-Fi selection, installation is cancelled, Wi-Fi is turned back off, and you return to Plugins with the installer still available. **Hide** removes only the Plugin Hub install entry; it does not disable plugins or affect sideloaded plugins. **Cancel** leaves the entry unchanged.
  - **Hide** persists across reboots. Setting **Settings → System → Plugins** to **Enabled** again brings the installer back.
  - Plugin Hub is optional. Users who prefer manual installation can download it directly from [crosspoint-plugin-hub releases](https://github.com/crosspoint-reader/crosspoint-plugin-hub/releases) and copy/extract the `pluginhub` folder so it ends up at `/.crosspoint/plugins/pluginhub/`, `/plugins/pluginhub/`, or `/.plugins/pluginhub/`. Once installed, Plugin Hub appears as a normal entry under **Home → Plugins**.

- **About**: Device and firmware information.

#### 3.6.8 OPDS Servers (Multiple Libraries)

CrossPoint supports saving multiple OPDS servers and switching between them when browsing catalogs.

1. Open **Settings → Network → OPDS Servers**.

2. Select **Add Server** to create a new entry, or select an existing server to edit it.

3. Configure these fields:
   
   - **Server Name**: Optional display name (for example, "Home Calibre" or "Public Catalog").
   
   - **OPDS Server URL**: Full catalog root URL (for Calibre Content Server, usually ends with `/opds`).
   
   - **Username / Password**: Optional credentials for authenticated servers.

4. Use **Delete Server** inside a server entry to remove it.

Behavior notes:

- You can store up to 8 OPDS servers.
- OPDS authentication supports HTTP Basic auth. If you use Calibre Content Server with authentication enabled, set it to Basic (not Digest).

You can also manage OPDS servers from the web interface while in File Transfer mode:

1. Connect to the device web UI.
2. Open `http://<device-ip>/settings`.
3. Use the **OPDS Servers** card to add, edit, or delete entries.

For web-based Wi-Fi network management, see [Web Settings (Wi-Fi + OPDS)](#369-web-settings-wi-fi--opds).

#### 3.6.9 Web Settings (Wi-Fi + OPDS)

While in **File Transfer** mode, the web settings page includes management cards for both **Wi-Fi Networks** and **OPDS Servers**.

1. On device: open **File Transfer** and connect through **Join a Network** or **Create Hotspot**.
2. In a browser, open `http://<device-ip>/settings` or `http://crosspoint.local`.
3. In **Wi-Fi Networks**, add, edit, or delete saved network entries (SSID + optional password).
4. In **OPDS Servers**, add, edit, or delete OPDS catalogs.

Behavior notes:

- Passwords are never shown back in the web UI after saving.
- Leaving Password blank while editing keeps the existing saved password unchanged.
- The web UI can save hidden-network SSIDs, but connecting to hidden networks still depends on the device-side Wi-Fi connection flow.

#### 3.6.10 KOReader Sync Quick Setup

CrossPoint can sync reading progress with KOReader-compatible sync servers.
It also interoperates with KOReader apps/devices when they use the same server and credentials.

**Server Type** controls which progress and metadata format CrossPoint sends:

- **CrossPoint** - For the default service or a self-hosted CrossPoint-compatible sync server. Sends standard KOReader progress plus CrossPoint's richer reading position when available.
- **KoSync** - For standard KOReader Sync servers such as `sync.koreader.rocks` or `koreader/kosync`. Uses the strict standard payload without CrossPoint-specific rich position data.
- **Other** - For custom servers that explicitly support CrossPoint's enhanced payload. Rich position is enabled, and when **Send Metadata** is on CrossPoint may also send recognized ISBN, ASIN, series name, and series index in addition to title/author metadata.

**Send Metadata** remains the privacy switch for bibliographic data. Turn it off if you want to sync progress without sending title, author, ISBN/ASIN, or series information.

##### Option A: CrossPoint Sync Server (`sync.crosspointreader.com`, default)

When **Sync Server URL** is left empty, CrossPoint uses the free CrossPoint sync server at `https://sync.crosspointreader.com`. It speaks the standard KOReader sync protocol (so KOReader apps can use it too). CrossPoint records page starts as chapter-content offsets and sends the corresponding standard KOReader XPath, so devices with different fonts or layouts can return to the same text.

1. On each CrossPoint device:

   - Go to **Settings → Network → KOReader Sync**.

   - Set **Username** and **Password** (enter the plain password; CrossPoint computes MD5 internally, and use the same values on all devices).

   - Leave **Sync Server URL** empty (or set it to `https://sync.crosspointreader.com`).

   - Set **Server Type** to **CrossPoint**.

   - On the first device, run **Sign Up** once to create the account directly from the device. On every other device, just run **Authenticate**.

Accounts are per server. Existing `sync.koreader.rocks` credentials do not exist on the CrossPoint server; either sign up again with the same username/password or use Option B to keep using the legacy server.

##### Option B: Legacy Public KOReader Server (`sync.koreader.rocks`)

Use this if you already sync KOReader devices against the official public server.

1. On each CrossPoint device:

   - Go to **Settings → Network → KOReader Sync**.

   - Set **Sync Server URL** to `https://sync.koreader.rocks` (required; an empty URL now points at the CrossPoint server instead).

   - Set **Server Type** to **KoSync**.

   - Set **Username** and **Password** to your existing KOReader Sync credentials.

   - Run **Authenticate**.

2. If you do not have an account yet, run **Sign Up** on the device, or register once with curl:

```bash
USERNAME="user"
PASSWORD="pass"
PASSWORD_MD5="$(printf '%s' "$PASSWORD" | openssl md5 | awk '{print $2}')"

curl -i "https://sync.koreader.rocks/users/create" \
  -H "Accept: application/vnd.koreader.v1+json" \
  -H "Content-Type: application/json" \
  --data "{\"username\":\"$USERNAME\",\"password\":\"$PASSWORD_MD5\"}"
```

When this returns `HTTP 402` with `{"code":2002,"message":"Username is already registered."}`, pick a different username or use that existing account.

##### Option C: Self-Hosted Server (Docker Compose)

1. Start a sync server:

```bash
mkdir -p kosync-quickstart
cd kosync-quickstart

cat > compose.yaml <<'YAML'
services:
  kosync:
    image: koreader/kosync:latest
    ports:
      - "7200:7200"
      - "17200:17200"
    volumes:
      - ./data/redis:/var/lib/redis
    environment:
      - ENABLE_USER_REGISTRATION=true
    restart: unless-stopped
YAML

# Docker
docker compose up -d

# Podman (alternative)
podman compose up -d
```

> [!NOTE]
> `ENABLE_USER_REGISTRATION=true` is convenient for first setup. After creating your users, set it to `false` (or remove it) to avoid unexpected registrations.

2. Verify the server:

```bash
curl -H "Accept: application/vnd.koreader.v1+json" "http://<server-ip>:17200/healthcheck"
# Expected: {"state":"OK"}
```

3. Register a user once.
   CrossPoint authenticates against KOReader Sync (`koreader/kosync`) using an MD5 key, so register using the MD5 of your password:

> [!WARNING]
> Sending a reusable MD5-derived password over plain HTTP is insecure.
> Create unique sync-only credentials and do not reuse main account passwords.
> Prefer `https://<server-ip>:7200` whenever traffic leaves a fully trusted LAN or when using untrusted networks.
> Use `curl -k` only for self-signed certificate testing.

```bash
USERNAME="user"
PASSWORD="pass"
PASSWORD_MD5="$(printf '%s' "$PASSWORD" | openssl md5 | awk '{print $2}')"

curl -i "http://<server-ip>:17200/users/create" \
  -H "Accept: application/vnd.koreader.v1+json" \
  -H "Content-Type: application/json" \
  --data "{\"username\":\"$USERNAME\",\"password\":\"$PASSWORD_MD5\"}"
```

If this returns `HTTP 402` with `{"code":2002,"message":"Username is already registered."}`, the account already exists.

4. On each CrossPoint device:
   
   - Go to **Settings → Network → KOReader Sync**.
   
   - Set **Username** and **Password** (enter the plain password; CrossPoint computes MD5 internally, and use the same values on all devices).
   
   - Set **Sync Server URL** to `http://<server-ip>:17200`.

   - Set **Server Type** to **KoSync**.
   
   - Run **Authenticate**.

If you use the HTTPS listener, use `https://<server-ip>:7200` (`curl -k` only for self-signed certificate testing).

##### Syncing While Reading

Once any of the options above is set up, open the **[Reader Menu](#5-reader-menu)** while reading and select **Sync Progress**. To sync in one step, set one of the Home key's actions to **KOSync** in **Settings → Controls → Home Button Gestures**. On button-only devices, set **Settings → Controls → Long-press Menu** to **KOSync** and hold Confirm instead.

- **Sync Highlights** is off by default. Enable it only for a compatible CrossPoint Sync server; manual **Sync Progress** then uploads and downloads this book’s highlights too. Leave it off for ordinary KOSync servers.
- With **Sync Behavior** set to **Ask every time**, choose **Apply Remote** to jump to remote progress or **Upload Local** to push current progress.
- With **Sync Behavior** set to **Smart sync**, CrossPoint auto-resolves simple cases: upload when no remote progress exists, confirm and leave both unchanged when local and remote progress are already synchronized, upload when local progress is further ahead, or apply remote when remote progress is further ahead.

### 3.7 Sleep Screen

The **Sleep Screen** setting controls what is displayed when the device goes to sleep:

| Mode               | Behavior                                                                                                                     |
| ------------------ | ---------------------------------------------------------------------------------------------------------------------------- |
| **Dark** (default) | The CrossPoint logo on a dark background.                                                                                    |
| **Light**          | The CrossPoint logo on a white background.                                                                                   |
| **Custom**         | A custom image from the SD card (see below). Falls back to **Dark** if no custom image is found.                             |
| **Cover**          | The cover of the currently open book. Falls back to **Dark** if no book is open.                                             |
| **Cover + Custom** | The cover of the currently open book, shown only while actively reading. Falls back to **Custom** behavior when not reading. |
| **Transparent**    | A BMP or PNG overlay drawn over the current screen. Supports PNG and 32-bit BGRA alpha transparency, and treats white as transparent in regular BMPs. Falls back to **Dark** if no valid overlay image is found. |
| **None**           | A blank screen.                                                                                                              |

#### Cover settings

When using **Cover** or **Cover + Custom**, two additional settings apply:

- **Sleep Screen Cover Mode**: **Fit** (scale to fit, white borders) or **Crop** (scale and crop to fill the screen).
- **Sleep Screen Cover Filter**: **None** (grayscale), **Contrast** (black & white), or **Inverted** (inverted black & white).

#### Custom images

To use custom sleep images, set the sleep screen mode to **Custom** or **Cover + Custom**, then place images on the SD card:

- **Multiple Images (recommended):** Create a `.sleep` directory in the root of the SD card and place any number of `.bmp` images inside. One will be randomly selected each time the device sleeps. (A directory named `sleep` is also accepted as a fallback.)
- **Single Image:** Place a file named `sleep.bmp` in the root directory. This takes priority over the `.sleep`/`sleep` directories.

#### Transparent overlay images

To use transparent sleep overlays, set the sleep screen mode to **Transparent**, then place BMP or PNG files on the SD card:

- **Multiple Images (recommended):** Create a `.sleep-overlay` directory in the root of the SD card and place any number of valid overlay `.bmp` or `.png` images inside. One will be randomly selected each time the device sleeps. A directory named `sleep-overlay` is also accepted as a fallback.
- **Single Image:** Place `sleep-overlay.bmp` or `sleep-overlay.png` in the root directory. A root BMP takes priority over a root PNG, and both take priority over the `.sleep-overlay`/`sleep-overlay` directories.

Transparent overlay files are intentionally separate from normal sleep images. Regular BMP formats supported by CrossPoint are accepted; white pixels leave the existing screen unchanged. For per-pixel alpha transparency, use a PNG with an alpha channel or a 32-bit BGRA BMP with both visible and non-opaque pixels. Opaque white pixels in alpha images erase the content behind them.

> [!TIP]
> For best results:
> - For non-transparent **Custom** mode, use uncompressed BMP files with 24-bit color depth.
> - For **Transparent** mode, use a PNG or uncompressed 32-bit BGRA BMP for per-pixel alpha, or a regular BMP for white-as-transparent artwork.
> - X4: Use a resolution of 480x800 pixels to match the device's screen resolution.
> - X3: Use a resolution of 528x792 pixels to match the device's screen resolution.

> [!TIP]
> You can set an image as the sleep screen cover directly from the BMP image viewer in the **[Browse Files](#33-browse-files-screen)** screen.

---

### 3.8 Custom Fonts (SD Card)

Noto Serif and OpenDyslexic are optional SD card fonts. Install `NotoSerif` to use
the former built-in serif family. On upgrade, an existing Noto Serif selection
uses `NotoSerif` if installed; otherwise, the reader switches to Noto Sans. After
installing the family later, select it in **Settings → Reader → Text Settings → Reader Font Family**.

CrossPoint loads additional fonts from the SD card. Custom fonts can add Chinese, Japanese, Korean, and other scripts that the built-in reader fonts lack. If your device have external RAM, you can copy `.ttf`, `.otf`, and `.ttc` files directly. Otherwise, use `.cpfont` files made from those fonts. 

There are three ways to install fonts:

1. **Download from device (recommended):** Go to **Settings → Reader → Manage Fonts**, browse the available font families, and select one to download over Wi-Fi.
2. **Upload via web interface:** While in **File Transfer** mode, open the web UI and use the **Fonts** tab to upload `.cpfont` files. The Fonts tab does not accept TTF/OTF/TTC files.
3. **Manual SD card copy:** Copy `.cpfont` families from the [crosspoint-fonts repository](https://github.com/crosspoint-reader/crosspoint-fonts) to `/.fonts/` or `/fonts/`. If your device have external RAM, you can also copy TTF/OTF/TTC files there without conversion.

Once installed, custom fonts appear in **Settings → Reader → Text Settings → Reader Font Family** alongside the built-in fonts.

See [docs/sd-card-fonts.md](./docs/sd-card-fonts.md) for full installation details and SD card folder structure.

---

### 3.9 Apps

Apps are small Lua programs that run from the SD card. Each app is a folder in `/apps/` containing an `index.lua` file.

**Installing:** Copy the contents of the repository's `lua/apps/` folder to `/apps/` on the SD card. This includes the shared `.lib` folder that several apps depend on.

**Opening:** With the [nexlibrOS theme](#nexlibros-theme), tap the **Apps** icon on the Home screen. With any theme, open **Plugins** on the Home screen and choose **Apps**. Tap the **Home** key to leave an app (or press **Back** on button-only devices).

The bundled apps are:

| App | What it does | Where its files go |
| --- | --- | --- |
| **Calculator** | Four-function calculator with a live result. | — |
| **Calendar** | Month, week, and day views with US, Canadian, Christian, and Jewish holidays. Add notes to any day, and list birthdays or anniversaries one per line in `dates.txt` (e.g. `03-14 Pi Day` or `1996-07-02 Sam's birthday`). | `/apps/calendar/dates.txt`, notes in `/apps/calendar/notes/` |
| **Converter** | Converts between many units, and between currencies including historical exchange rates adjusted for inflation. | Bundled data files in `/apps/converter/` |
| **Counter** | A simple counter: press Left/Right to count. | — |
| **Crossword** | Plays Across Lite `.puz` crosswords. Progress is saved without changing the puzzle files. | Puzzles in `/apps/crossword/puzzles/` |
| **Dictionary** | Looks up any word in your installed StarDict dictionaries, outside of a book. | Uses `/dictionaries/` and `/.dictionaries/` |
| **Flashcards** | A very light Anki: reviews plain-text Anki deck exports with basic spaced repetition. | Decks (`.txt`, `.tsv`, `.csv`) in `/apps/flashcards/anki/` |
| **Gallery** | Photo browser and low-power slideshow for JPEG, PNG, and BMP images. Your sleep-image folders are listed too. | Photo folders in `/apps/gallery/` |
| **Notepad** | Notes and checklists, each saved as a Markdown file. | `/apps/notepad/notes/` |
| **Wikipedia** | Offline Wikipedia articles: search, random article, and followable links, shown in your reading font. Packs are built with `scripts/wikipedia/build_wiki_pack.py`. | `/apps/wikipedia/data/` |

Each app shows its own controls. Touch works where it makes sense, for example tapping keys, squares, or links. See [docs/lua-apps.md](docs/lua-apps.md) for writing your own apps.

---

## 4. Reading Mode

Once you have opened a book, the screen and keys switch to reading controls:

| Action | Touch (X4 Pro) | Keys |
| --- | --- | --- |
| **Next page** | Swipe left | **Side Down** |
| **Previous page** | Swipe right | **Side Up** |
| **Open the Reader Menu** | Tap the middle of the page | Long-press the **Home** key |
| **Add or remove a bookmark** | Tap the top-right corner | — |
| **Look up, highlight, or follow a word's link** | Long-press the word | — |
| **Follow a link or footnote** | Tap it | — |
| **Close the book** | — | Tap the **Home** key |

On the reading page, a right swipe always turns back a page, so the left-edge Back swipe does not close the book. Use the **Home** key instead.

### Page Turning

By default, pages turn by swiping: swipe left for the next page and right for the previous page. In right-to-left books the directions are reversed. The side keys also turn pages.

To turn pages by tapping instead, set **Next Page** and **Previous Page** in **[Controls Settings](#364-controls)** to "Tap & Swipe" or "Tap Only". Tapping the left third of the page then goes back, and tapping elsewhere goes forward. The middle of the page still opens the Reader Menu. To turn touch page-turning off completely, turn off **Touch Reader Controls** or use the tile in the frontlight panel.

The side keys can be swapped or disabled with **Side Button Layout** in the **[Controls Settings](#364-controls)**. If the **Short Power Button Click** setting is set to "Page Turn", you can also turn to the next page by briefly pressing the Power button.

**On button-only devices:** Press **Right** or **Side Down** for the next page, and **Left** or **Side Up** for the previous page.

### Chapter Navigation

Open the **[Reader Menu](#5-reader-menu)** and choose **Select Chapter** to jump to any chapter.

To skip chapters with the keys, set **Long-Press Button Behavior** in the **[Controls Settings](#364-controls)** to "Chapter Skip". Then hold **Side Down** (or **Right**) briefly and release for the next chapter, or **Side Up** (or **Left**) for the previous chapter.

### Auto Page Turn

Auto Page Turn automatically advances pages at a set interval, useful for hands-free reading. This feature can be enabled and configured from the **[Reader Menu](#5-reader-menu)** while reading an EPUB.

### Tilt Page Turn (X3 only)

On the **Xteink X3**, the gyroscope can be used to turn pages by tilting the device. This feature is available in the Controls settings.

### Links and footnotes

When reading an EPUB that contains footnotes, tap a footnote reference (or any link) to go to it. From the footnote, you can return to your original reading position. Links and footnotes can also be selected from **Links and Footnotes** in the **[Reader Menu](#5-reader-menu)**.

If the device sleeps or you close the book after following a link, the book reopens on the page you were viewing. Back still returns you to where you followed the link. The reader keeps the three most recent return positions.

### Dictionary Lookup

Words on the current page can be looked up in an offline StarDict dictionary stored on the SD card. Copy a dictionary to the `/dictionaries/` folder and select it in **Settings → Reader → Dictionary**.

Long-press a word on the page to select it. A bubble above the word offers:

- **Look Up**: show the word's definition (when a dictionary is selected). Tapping the selected word does the same.
- **Highlight** (or **Remove Highlight** on a highlighted word): see [Highlights](#highlights).
- **Go to**: follow the link the word belongs to (only shown on linked words).

Tap another word to move the selection there, or tap away from the text to return to the book. On the definition screen, tap the right side of the screen for the next page and the left third for the previous page, and go back to return to the book.

**Multiple dictionaries:** Every installed dictionary is searched. When several have the word, the definition screen shows a tab for each one. Your default dictionary comes first, and dictionaries tagged for the book's language are preferred. Tap a tab to switch between them. If a dictionary has several entries for the same word (for example a noun and an adjective), all of them are shown.

**On button-only devices:** Choose **Look Up** in the **[Reader Menu](#5-reader-menu)** (or hold **Confirm**, if the **Long-press Menu** setting in **[Controls Settings](#364-controls)** is set to "Dictionary"). Use **Left/Right** to highlight a word and press **Confirm** to show its definition. On the definition screen, press **Confirm** to switch dictionary tabs. On touch devices, the same **Look Up** menu entry returns if you turn off **Long-Press Word to Look Up**.

See [docs/dictionary.md](docs/dictionary.md) for supported formats, setup, language tags, and where to find dictionaries.

### Highlights

Highlights mark passages with a gray background that stays put when you change the font, margins, or orientation.

To highlight, long-press a word and choose **Highlight** from the bubble, then drag the selection handles and tap **Highlight**. To remove one, long-press a highlighted word and choose **Remove Highlight**, or delete it from the **Highlights** list.

**On button-only devices:** Choose **Highlight** in the **[Reader Menu](#5-reader-menu)**. Move to the first word and press **Confirm**, move to the last word, press **Confirm** again, then choose **Highlight**. Remove highlights from the **Highlights** list.

See [Highlights in detail](#53-highlights) for limits, syncing and export. Highlights made with earlier firmware move to the new store automatically the first time you open each book.

### System Navigation

* **Return to Home:** Tap the **Home** key to close the book and return to the **[Home](#31-home-screen)** screen. **Go Home** in the Reader Menu does the same.
* **Reader Menu:** Tap the middle of the page, or long-press the **Home** key, to open the **[Reader Menu](#5-reader-menu)**, which includes chapter navigation, reading options, and more. Both can be changed in **[Controls Settings](#364-controls)** (**Show Reader Menu** and **Home Button Gestures**).

**On button-only devices:**

* **Return to Home:** Press **Back** to close the book and return to the Home screen.
* **Return to Browse Files:** Press and hold **Back** to close the book and return to the **[Browse Files](#33-browse-files-screen)** screen. **Short Back to File Browser** in Controls Settings swaps the short and long press.
* **Reader Menu:** Press **Confirm**.
* **Long-press Confirm (configurable):** Holding **Confirm** runs the function chosen by the **Long-press Menu** setting in **[Controls Settings](#364-controls)** — "Bookmark" (default) drops a bookmark, "KOSync" launches KOReader Sync, "Dictionary" starts a word lookup, "Highlight" starts highlight selection, "Disabled" does nothing. A short press always opens the Reader Menu.

### Supported Languages

CrossPoint renders text using the following Unicode character blocks, enabling support for a wide range of languages:

* **Latin Script (Basic, Supplement, Extended-A/B):** Covers English, German, French, Spanish, Portuguese, Italian, Dutch, Swedish, Norwegian, Danish, Finnish, Polish, Czech, Hungarian, Romanian, Slovak, Slovenian, Turkish, Catalan, and others.
* **Cyrillic Script (Standard and Extended):** Covers Russian, Ukrainian, Belarusian, Bulgarian, Serbian, Macedonian, Kazakh, Kyrgyz, Mongolian, and others.
* **Vietnamese:** Supported via extended Latin glyph coverage in the built-in reader fonts.

The UI includes Arabic and Hebrew (menus use built-in fonts with presentation-form coverage). Built-in **reader** fonts do not cover Chinese, Japanese, Korean, Arabic, Greek, Hebrew, or Farsi for book text. **CJK, Hebrew, Arabic, Greek, and other extended scripts can be enabled for reading by installing custom SD card fonts** — see [Custom Fonts (SD Card)](#38-custom-fonts-sd-card).

---

## 5. Reader Menu

Tap the middle of the page (or long-press the **Home** key) while reading to open the Reader Menu. From here you can access reading utilities and navigation options without leaving the book. Tap an option to choose it. On button-only devices, press **Confirm** to open the menu.

Available options include:

- **Select Chapter** – Open the table of contents to jump to a specific chapter (see [Chapter Selection](#51-chapter-selection) below).
- **Links and Footnotes** – Select an internal link on the current page. This option appears when the page contains links.
- **Bookmarks** – List this book's bookmarks (see [Bookmarks](#52-bookmarks)) *(only shown once the book has a bookmark)*.
- **Toggle Bookmark** – Add or remove a bookmark on the current page.
- **Night Mode** – Invert the screen.
- **Frontlight** – Adjust the frontlight *(frontlight devices only)*.
- **Look Up** – Select a word on the current page and show its dictionary definition (see [Dictionary Lookup](#dictionary-lookup)). Requires a dictionary to be selected in **Settings → Reader → Dictionary**.
- **Highlight** – Select a passage to highlight (see [Highlights](#highlights)).
- **Highlights** – List this book's highlights *(shown after the first highlight is saved)*.
- **Reading Orientation** – Cycle through screen orientations without leaving the reader.
- **Auto Turn (Pages Per Minute)** – Cycle through automatic page turn speed options for hands-free reading.
- **Go to %** – Jump to a specific position in the book by percentage.
- **Take screenshot** – Save a screenshot of the current page to the `screenshots/` folder.
- **Show page as QR** – Display a QR code encoding the current reading position.
- **Go Home** – Close the book and return to the Home screen.
- **Sync Progress** – Push or pull reading progress with a KOReader sync server (see [KOReader Sync Quick Setup](#3610-koreader-sync-quick-setup)).
- **Delete Book Cache** – Clear the cached layout data for the current book, forcing a re-index on next open.
- **Text Settings** – Change the font, size, spacing, and other text layout settings without leaving the book.

On touch devices, **Look Up** and **Highlight** only appear when **Long-Press Word to Look Up** is turned off, since long-pressing a word offers both.

To close the menu and return to your current page, swipe right from the left edge or tap the back arrow (press **Back** on button-only devices).

### 5.1 Chapter Selection

Accessible by selecting **Select Chapter** from the Reader Menu.

1. Swipe up or down to find the chapter you want.
2. Tap it to jump to that chapter.
3. *Alternatively, go back to cancel and return to your current page.*

**On button-only devices:** Use **Left** (or **Side Up**) and **Right** (or **Side Down**) to highlight a chapter, press **Confirm** to jump to it, or press **Back** to cancel.

---

### 5.2 Bookmarks

Bookmarks can be created to quickly save and restore your place in a book.

To bookmark a page, tap its top-right corner. A popup confirms the bookmark and disappears after a couple of seconds. A bookmarked page shows a small gray triangle in that corner. Tap the corner again to remove the bookmark.

To see your bookmarks, open the **[Reader Menu](#5-reader-menu)** and choose **Bookmarks**. Tap a bookmark to jump to that place in the book. Long-press a bookmark to delete it, then confirm or cancel.

**On button-only devices:** Hold **Confirm** for about half a second to add a bookmark, or to remove the current page's bookmark. In the Bookmarks list, press **Confirm** to open a bookmark, or hold **Confirm** for about 0.7 seconds and press **Confirm** again to delete it (**Back** cancels).

Bookmarks are stored in the `.crosspoint/bookmarks` folder in the JSON format.

### 5.3 Highlights

Select **Highlight** from the Reader Menu, move to the first word, and press **Confirm**. Move to the last word and press **Select** again to choose **Look Up**, **Highlight**, or **Bookmark**. **Back** closes that popup without losing the selection. On touch devices, choose **Highlight** from the long-press bubble; with **Long-Press Word to Look Up** turned off and **Touch Reader Controls** on, press and hold a word to go straight to selection. Drag either selection handle to adjust the range, then tap **Look Up**, **Highlight**, or **Bookmark** above it. Hold the ending handle at the bottom edge or over the final word for one second to extend onto the next preloaded page. Selection covers up to three pages in the same chapter, subject to the 240 selectable-word limit. The toolbar overlays the page without shifting the text. Releasing a handle keeps the selection open; tapping outside the text dismisses it. **Look Up** searches the selected text in your dictionary; **Bookmark** toggles the bookmark on the selection's final page. A new highlight that overlaps existing ones replaces them: the gray area covers all of them, but the saved text is only your latest selection. Saved ranges with source offsets remain highlighted after changes to font, spacing, orientation, or pagination. Ranges without source offsets can only be highlighted when their saved layout still matches.

Select **Highlights** to list this book's highlights in reading order. Tap one, or press **Confirm**, to jump to it. Long-press it, or hold **Confirm** for about 0.7 seconds, to choose **Open** (read the saved text) or **Delete**. Highlights without a saved text offset open their text instead of jumping. Deletions are sent on the next manual sync when **Sync Highlights** is enabled.

Each book can store up to **1,024 highlights on ESP32-S3 devices** or **256 on ESP32-C3 devices**. These limits also apply to highlights downloaded through sync.

The store under `.crosspoint/clippings` keeps up to **4 KiB of UTF-8 text per highlight**. Longer selections must be shortened before saving. CrossPoint also appends up to **2,000 UTF-8 bytes** of each saved excerpt to `My Clippings.txt` in the root of the SD card, using Kindle-compatible formatting. If that export fails, the highlight remains saved and the message reports the export failure.

Local book and folder moves carry highlights with them. Deleting a local book removes its local highlights; it does not delete already synced server copies or historical entries in `My Clippings.txt`. Deleting an individual highlight likewise leaves the text export untouched, but queues its server deletion for the next enabled manual sync. `My Clippings.txt` is an independent append-only export, not the source used to restore or sync highlights.

## 6. Current Limitations & Roadmap

Please note that this firmware is currently in active development. The following features are **not yet supported** but are planned for future updates:

* **Cover Images:** Large cover images embedded into EPUB require several seconds (~10s for ~2000 pixel tall image) to convert for sleep screen and home screen thumbnail. Consider optimizing the EPUB with e.g. https://github.com/bigbag/epub-to-xtc-converter to speed this up.
* **Image Support:** Most JPG, PNG and non-interlaced GIF images in EPUBs render correctly, including book covers. Animated GIFs show only the first frame. Interlaced GIFs are not supported and use an outlined image placeholder.

---

## 7. Troubleshooting Issues & Escaping Bootloop

If an issue or crash is encountered while using Crosspoint, feel free to raise an issue ticket and attach the logs.

**Crash reports on SD card:** After a crash, CrossPoint automatically saves a crash report to the SD card (no USB connection needed). Check the root of the SD card for a crash log file and include it with any bug report.

**Serial monitor logs:** For more detailed debugging, connect the device to a computer and run the custom debugging monitor script (requires Python 3 with `pyserial`, `colorama`, and `matplotlib`; install via `pip3 install pyserial colorama matplotlib`):

```
python3 scripts/debugging_monitor.py
```

The script auto-detects the serial port. You can also specify one explicitly:

```
python3 scripts/debugging_monitor.py /dev/ttyACM0        # Linux
python3 scripts/debugging_monitor.py /dev/tty.usbmodem1  # macOS
python3 scripts/debugging_monitor.py COM7                # Windows
```

**Features:**

- Color-coded log output by category (errors, memory, display, EPUB parsing, etc.)
- Live memory usage graph (free RAM, total RAM, max contiguous allocation) updated every second
- Interactive command prompt — type a command and press Enter to send it to the device
- Screenshot capture — saves the current display to `screenshot.bmp` when triggered by the device

**Options:**

| Option               | Description                                               |
| -------------------- | --------------------------------------------------------- |
| `--baud RATE`        | Baud rate (default: 115200)                               |
| `--filter KEYWORD`   | Show only lines containing the keyword (case-insensitive) |
| `--suppress KEYWORD` | Hide lines containing the keyword (case-insensitive)      |

**Examples:**

```
# Show only memory-related log lines
python3 scripts/debugging_monitor.py --filter MEM

# Hide noisy SD card log lines
python3 scripts/debugging_monitor.py --suppress "[SD]"
```

Press **Ctrl-C** or close the graph window to exit.

If the device is stuck in a bootloop because a book crashes when it opens, restart it: after a crash while opening a book, the next start goes to the Home screen instead of reopening the book. On button-only devices you can also force this: press and release the Reset button, then press and hold the configured Back button and the Power button to boot to the Home Screen.

There can be issues with broken cache or config. In this case, delete the `.crosspoint` directory on your SD card (or consider deleting only `settings.json`, `state.json`, or `epub_*` cache directories in the `.crosspoint/` folder).
