# Changes
## v4.4.3
A maintenance release that drops the built-in updater, adds folder sharing and a window pin on top of VNote 4.4.2:

* **Share Folder** (Notebook explorer context menu): export a folder as a self-contained, movable bundle you can hand to someone else or drop into another notebook
    * Notes you currently have open are saved first, so the bundle always carries their latest content
    * The copy is verified before it is published, and a failed export leaves nothing half-written behind
    * Refuses to follow symbolic links, junctions and reparse points, and warns up front when two names would collide on a case-insensitive destination filesystem
* **Stay on Top**: detached view windows now have a pin so a window can be kept above the others
* **The built-in updater has been removed.** VNote no longer downloads or installs anything, and never modifies its own installation directory. It simply checks for a newer release and, when one exists, tells you and offers to open the **release page**, where you download and install the new version yourself.
    * Fixes VNote failing to start from a read-only or non-writable installation directory (`/usr/bin`, `Program Files` for a standard user, a read-only DMG), where the old updater's startup lease could not be created (issue #2728). Per-machine MSI installs were affected on Windows too.
    * The **Update source** setting (Settings › General) still selects GitHub or Gitee. **New installations now default to Gitee**; an existing installation keeps whatever it already had, and no setting is migrated.
    * If you used the in-app updater before, you can safely delete the leftover `.vnote-old` and `.vnote-update` folders next to `vnote.exe`. VNote no longer creates or reads them.
* **Translations**: Simplified Chinese and Japanese catalogs updated for the new strings

## v4.4.2
A feature release that adds a built-in updater, a reworked PDF viewer, detachable view windows, and a Gitee release mirror on top of VNote 4.3.0:

* **In-app updates** (new, Windows x64, Qt 6 build): VNote can now update itself
    * Downloads only what changed — a release publishes a signed file manifest plus an optional delta package, and the client resolves a delta chain before falling back to the full package
    * Every manifest is verified against a compiled-in Ed25519 key; an unsigned or unverifiable release is refused (fail-closed), and hosts are allow-listed per release source
    * Files are staged inside the install directory and swapped in under a durable write-ahead journal **at exit**, with automatic rollback and crash recovery on the next launch
    * A machine-wide lease serializes updates across sessions; Microsoft Store and Program Files installs are detected and directed to their own update channel
    * Surfaces as an **Update** dialog (Update / Skip This Version / Later / Restart Now) and a startup notification with inline progress
    * **Update source** setting (Settings › General) to fetch updates from either GitHub or Gitee
    * The Windows 7 (Qt 5.15) build publishes no update packages; there the check simply points at the release page
* **PDF viewer**
    * Upgraded the bundled pdf.js from 3.1.81 to **3.11.174** (the last v3 release; v4+ requires Chrome 125+, which the Qt 5.15 / Windows 7 build cannot provide), and vendored the previously-missing `web/standard_fonts/` so PDFs relying on non-embedded Helvetica/Times/Courier render correctly
    * The PDF's embedded outline (bookmarks) now populates the **Outline** dock and a new per-window Outline toolbar popup, with click-to-jump
    * Fix the external file path not resolving for drag-and-dropped PDFs
* **Views & windows**
    * **Detach** a view window into a separate top-level window from the tab context menu
    * New `--detached-view` CLI option to open files directly in a detached split
    * `Ctrl+G, X` closes the focused dock or, when the editor has focus, the current tab
    * The Console dock activation shortcut is now configurable
* **Notifications**: attention routing, incident de-duplication, and a toast surface, so repeated failures (image upload, auto-save, sync) collapse into one actionable entry with a **Details** view
* **Editor & Markdown**
    * Offer to migrate a note's legacy `vx_images` pictures into its assets folder, with an inline banner and a **Don't Ask Again** choice
    * Edit-mode **Copy Link** action for headings
* **United Entry**: new `task` entry to find and run tasks, a popup sized from the UI font and clamped to the screen, and a readable selected task row
* **Settings**: a Quick Note scheme can now use a note template
* **macOS**: capture selected text from any app as a note through a system Service
* **Tags**: `Esc` clears the selection in the Tags explorer
* **Fixes**
    * Recover from a partially-installed bundled resource set instead of leaving stale data behind
    * Keep the frameless title bar out of Qt's menu-bar slot eviction
    * Fix the export dialog not remembering the last-used theme (#2389)
    * Fix enable-sync failing on macOS when opening a remote notebook (#2718)
    * Stop duplicating legacy attachments during VNote3 migration
    * UTF-8-safe `.vswp` backup path (#2721)
    * Bundle the MSVC CRT for toolsets newer than CMake's redistributable list
    * Use Git Bash explicitly in `init.cmd` instead of relying on WSL bash
    * Remove the version-specific config override logic
* **Internals**
    * `core_services` no longer links VTextEdit or Qt Widgets
    * Inline chrome is themed through the stylesheet instead of hardcoded colors, with a test gate that fails the build on literal colors in `setStyleSheet()`
* **Infrastructure**: code and releases are mirrored to Gitee, keeping the two most recent releases
* **Packaging (Windows)**: the release ZIP is now flat — `VNote-<ver>-win64/vnote.exe` instead of `VNote-<ver>-win64/bin/vnote.exe`. The `bin/` level was unintended (`src/CMakeLists.txt` asked for a flat install, but a submodule's `include(GNUInstallDirs)` seeded the CMake cache first) and it is incompatible with the incremental updater, which strips exactly one level from a full package
* **Translations**: updated Simplified Chinese and Japanese translations

## v4.3.0
A feature release that adds an in-app notification system, a Tasks dock, a reworked export experience, and a column-based editor status bar on top of VNote 4.2.0:

* **Notifications** (new): an in-app notification system with a toolbar button and popup, plus **Clear All** / **Dismiss** actions
* **Tasks**
    * New **Tasks** dock and **Console** log viewer, with in-app editing of task files
    * The task subsystem was migrated to the ServiceLocator/DI architecture (the legacy task-output console was removed)
    * Snippets and Tasks docks gained a title-bar search filter
* **Status bar**: Text and Markdown view windows now use a column-based status bar with Material-style chip theming and a **Spelling** control (enable spell check / auto-detect language)
* **Export**
    * Built-in export schemes with a **New / Duplicate / Delete** split button
    * Split-pane workspaces can now be selected as export sources
    * Configurable **PDF header/footer** for wkhtmltopdf export (supports `[page]`, `[title]`, `[date]` placeholders)
    * MathJax equations are rasterized to PNG for docx/custom export, and exported syntax colors resolve the real `highlight.css`
* **Editor & Markdown**
    * **Code block line wrap** option for read mode
    * **Customize global styles** button to open `user.css` for styles applied under every theme
    * Syntax highlighting for display math (`$$`) source in the editor
    * `Ctrl+Left-Click` opens a Markdown link in edit mode; external links now prompt before opening
    * Snippet expansion and the `@@` cursor mark are honored in note templates and Quick Note folder paths
    * Manual **file-encoding override** to fix non-UTF-8 mojibake
* **Workspaces & views**
    * **Remove Other Workspaces** corner-menu action
    * Double-clicking the empty tab-bar area triggers Quick Note
    * A **View** action was added to the read-mode image context menu
* **United Entry**: collapses to an accent toolbar icon and expands to an input, with trigger on list-row activation
* **General**: a global wake-up hotkey to show the main window, a `--quiet` startup flag, `--remote-debugging-port` for QtWebEngine, and updated bundled Mermaid (v11.16.0)
* **Security**
    * Fix stored XSS via YAML frontmatter (GHSA-vfhj-c636-h59x)
    * Prevent external-program argument injection (CWE-88)
* **Fixes**
    * Fix the search keyword combobox growing unshrinkably wide
    * Fix a missing home dashboard after removing a hidden workspace
    * Fix `\label` equations rendering as black boxes, Graphviz render errors cascading, SVG data-URI images not displaying, and graph popup preview for nodes with line breaks
    * Make Cancel on the external-change dialog a sticky ignore
* **Translations**: updated Simplified Chinese and Japanese translations

## v4.2.0
A feature release that adds a Personal Knowledge Management **home dashboard**, cross-notebook history, activity tracking, and a dedicated Sync settings page on top of VNote 4.1.1:

* **Dashboard** (new): a customizable PKM home tab built from movable, resizable **stickers**
    * Built-in **Calendar**, **Greeting**, and **History** stickers
    * **History** sticker with recent-files and calendar-day modes, plus a one-click clear-date-filter
    * **Lock/Unlock** mode to prevent accidental layout changes, an **Add Sticker** menu, a per-sticker **Resize** dialog, and **Reset Dashboard** to restore the default layout
    * Toolbar and calendar icons follow the active theme
* **History**: reworked into a cross-notebook `HistoryService`, so recent files are aggregated across all notebooks (the legacy per-notebook history panel and option were removed)
* **Activity tracking**: focus time and note activity are now tracked via vxcore
* **Settings**: new **Sync** page; the auto-sync interval moved there and now defaults to 120s
* **Editor**
    * Re-added the **In-Place Preview** toggle to the Markdown toolbar
    * The image-host button now appears only in edit mode
    * Removed the markdown editor **Override Font** option and its settings UI
* **Windows switcher**: a new **Windows** entry lists open view windows across workspaces, with prefix-matching and focus-restore fixes
* **Title bar**: the current note name is now shown in the system title bar
* **Fixes**
    * Fix opening files from the command line / "Open with VNote", and absolutize forwarded open-file paths at the IPC boundary
    * Preserve heading anchors in **Insert As Relative Link**
    * Avoid a pre-`QApplication` event-loop warning by lazily starting the search/image-host worker threads
* **Translations**: updated Simplified Chinese and Japanese translations

## v4.1.1
A maintenance release with search improvements, PDF export fixes, and editor polish on top of VNote 4.1.0:

* **Search**
    * Content-search results now stream in incrementally, so matches appear as they are found instead of after the whole search completes
    * The maximum number of search results is now configurable
    * Content matches are grouped by line, removing duplicate result rows
* **Export**: fix squeezed or mis-sized MathJax equations in PDF export, and stop exports from hanging on documents that mix equations with Mermaid/Graphviz/Flowchart/WaveDrom diagrams
* **Editor**
    * Fix emoji next to styled text (such as headings) rendering as tofu boxes
    * Refresh the editor style immediately when switching themes
* **Notebooks (raw)**: enable image paste, and preserve note timestamps and metadata across rename
* **Startup**: merge instead of overwrite `QTWEBENGINE_CHROMIUM_FLAGS`, so user-set Chromium workaround flags (e.g. `--single-process`, `--disable-gpu`) are respected
* **macOS**: always use the system title bar

## v4.1.0
VNote 4 is a major release built on a brand-new native core (**vxcore**) that powers notebook management, search, configuration, and synchronization. The whole application has been re-architected (clean MVC + dependency injection) for reliability and future extensibility. Highlights versus VNote 3:

* **Notebook Sync** (flagship): keep notebooks in sync across devices through any Git remote (GitHub, GitLab, self-hosted) over HTTPS or local `file://` repositories
    * Enable per notebook from the new **Sync** button and **Sync Info** panel in the notebook explorer
    * Authenticate with a Personal Access Token, stored securely in your system keychain (Windows Credential Manager, macOS Keychain, or Linux Secret Service) — never written to config or logs
    * Changes sync automatically in the background, with a configurable **Auto-sync interval** in Settings > General (or sync on demand with **Sync Now**)
    * Built-in conflict resolution when the same note is edited on two devices
    * Live status (last sync time, idle/syncing/conflict/error) plus actionable messages for authentication failures
* **Open & clone notebooks**: the redesigned **Open Notebook** dialog supports a **Local folder** mode and a **Remote URL** mode that clones a notebook straight from a Git URL (with optional token and mid-clone cancel)
* **Read-only notebooks**: open a remote notebook without a token to browse it read-only; a lock badge marks read-only notebooks and tabs, and editing actions are safely disabled. Add a token later to enable editing
* **VNote 3 migration**: import and convert existing VNote 3 notebooks via **Open VNote3 Notebook**
* **Filesystem-aware notebooks**
    * Detects notes whose files were deleted on disk, renders them dimmed, and offers a batch "remove missing items from the notebook" prompt
    * Show/import external (unindexed) files, per-notebook ignore list, and **Rebuild Database** for index maintenance
    * "Remove from Notebook" to drop a note from the index while keeping the file on disk
    * Raw vs Bundled notebook types
* **Notebook explorer**: drag-and-drop reordering and a manual **Sort** dialog (Top/Up/Down/Bottom), per-node **Mark** (custom text/background color), **Pin to Quick Access**, and multiple view orders (configuration, name, created/modified time)
* **Editor & views**
    * Split panes (vertical/horizontal), move tabs across splits, maximize and evenly distribute splits, with full session layout persistence
    * Per-tab **Reload** and **Auto Reload** with scroll-position preservation, and external-change detection
    * **Reopen last closed tab** (`Ctrl+Shift+T`)
    * Markdown editor with live preview, side-by-side edit/preview, in-place code/math block preview, MathJax, Mermaid, flowchart.js, WaveDrom, PlantUML and Graphviz diagrams, syntax highlighting, image paste and image-host upload
    * Plain-text, PDF, and mind-map view windows
* **Responsiveness**: note saving now runs off the UI thread, so large files, network drives, or antivirus scans no longer freeze the editor; saves and Git operations on the same notebook are serialized for safety
* **Tags & search**: hierarchical tag tree with per-note tagging; search by File Name / Content / Tag across Buffers / Folder / Notebook / All Notebooks, with case-sensitivity, regex and file-pattern options
* **Docks**: Notebook explorer, Outline, Tag, Search, Snippet, History, Quick Access / Location List, and Console panels
* **Themes**: 10 built-in themes spanning light and dark (Everforest, Moonlight, Pure, Native, Solarized Light/Dark, VSCode Dark, Vue Light/Dark, VX-Idea) with one-click switching
* **Platforms**: Windows, Linux, and macOS (universal Intel + Apple Silicon); built with Qt 6, with a Qt 5.15.2 build still provided for Windows 7 compatibility

## v3.20.0
* MindMap: add outline and linking support
* Refine themes
* Support searching tags
* Enhanced QuickAccess with unique id
* NoteExplorer: support customizing node's color/background/outline
* MarkdownEditor: support copying local GIF
* Fix attachment and tag popup
* InputMode: add VSCode input mode and keep the default Normal input mode simple

## v3.19.2
* Codesign MacOS Bundle
* Fix toolbar expansion button style
* Support hot-reloading of theme via --watch-themes option

## v3.19.1
* Fix toolbar button in Qt 6.8

## v3.19.0
* Add VSCode-sytle editor shortcuts

## v3.18.1
* Fix crash caused by Qt6 change
* Fix XSS protection exemption
* Check link before open

## v3.18.0
* Upgrade to Qt6
* Support MacOS universal build
* Upgrade Mermaid, Flowchart.js, and markdown-it
* Markdown-it
    * Fix XSS protection and turn it on by default
    * Support mark by `==xx==`

## v3.17.0
* Quick note: create note in given scheme (@feloxx)
* MarkdownEditor: support inserting multiple images (@feloxx)
* Mermaid: upgrade and fix preview issue (@ygcaicn)
* Flowchart.js: upgrade

## v3.16.0
* Support reading PDF format
* Support Ming Map editor in suffix `*.emind`
* Support "View By" for notebooks selector
* ViewWindow: add shortcut Ctrl+G,V to alternate among view modes
* Bug fixes

## v3.15.1
* Add two themes
* Bug fixes

## v3.15.0
* Editor supports Word Count
* Add Open Windows panel
* Theme: add Vue-light theme
* Support default open mode
* NotebookSelector: support dynamic icons for notebooks

## v3.14.0
* Theme: support custom icons
* Theme: refine icons
* NavigationMode: fix issue for input method

## v3.13.1
* Shortcuts for Copy/Paste/Properties in node explorer
* Global shortcut to call out main window
* UnitedEntry: bug fix for macOS

## v3.13.0
* United Entry: migration of Universal Entry

## v3.12.888
* Fix shortcuts in key sequence with input method (like `Ctrl+G, E`)
* Add line ending settings for config files
* FindAndReplace: fix zero-length search
* QuickAccess: support folders
* Upgrade to Qt 5.15.2
* Support file associations
* NewNoteDialog: remember default file type

## v3.12.0
* NotebookExplorer: support separate node explorer
* Theme: add user-provided VSCode-Dark theme
* MarkdownEditor: use web to highlight code blocks
* MarkdownViewWindow
    * Add switch for code block line number
    * Fix ParseToMarkdown `<style>` issue
    * Add config for overridding MathJax script
* SortDialog: fix sorting issue of date
* FramelessMainWindow: fix StayOnTop issue

## v3.11.0
* Task: support a simple task system (@tootal)
* Theme: add user-provided Solarized-Dark and Solarized-Light themes
* Export: fix wkhtmltopdf table-of-contents translation
* Support equation begin in MathJax
* MainWindow: decide DPI on the screen vnote starts
* Settings: support searching
* Fix crash caused by Youdao Dict

## v3.10.1
* MarkdownEditor: fix view mode issue
* Support print
* Refine icons

## v3.10.0
* MarkdownEditor
    * Support side-by-side edit with preview
    * Support config for highlighting whitespace
* Tag: fix input method issue on macOS

## v3.9.0
* Remove recycle bin node (now recycle bin is just a simple folder)
* Quick Access: support removing items directly
* MarkdownEditor
    * Support centering images in read mode
    * Add user.css for user styles in read mode
    * Add debugger by F12
    * Support context-sensitive context menu for images and links

## v3.8.0
* Support tags
* Introduce notebook database using SQLITE
* A perfect frameless main window on Windows
* Add switch to control whether store history in notebook
* Refine dock widgets of main window
* NotebookExplorer: support scan notebook and import external files

## v3.7.0
* PlantUml/Graphviz: support relative path executable
* macOS: support opening file with VNote in Finder
* Sort notes by name case-insensitively
* Export
    * Support All-in-One in PDF format
    * Support Custom export format (like Pandoc)
    * Allow minimizing the export dialog and doing export at background
* MainWindow: use icon-only bar for docks
* Support update check
* Add shortcuts for CloseOtherTabs and CloseTabsToTheRight
* Search: highlight matched items in opened files
* Editor: support specifying line ending

## v3.6.0
* Support **Image Host**: GitHub and Gitee
* Add config page for Vi

## v3.5.1
* LocationList: fix recently introduced regression when highlighting segments of text

## v3.5.0
* Support History
* ViewArea
    * `Ctrl+G, H/J/K/L` to navigate through ViewSplits
    * `Ctrl+G, Shift+H/J/K/L` to move ViewWindow across ViewSplits
* MarkdownEditor
    * Add configs for in-place preview sources
    * Add a tool button to disable in-place preview
* Vi: support align and indent commands `=` and `>`
* LocationList: highlight matched text segments
* SelectDialog: support shortcuts (such as Rich Paste)

## v3.4.0
* Support Snippet
    * `Ctrl+G S` to insert a snippet
    * `%snippet_name%` to insert a snippet (the legacy Magic Word)
        * Snippet is supported in some dialogs (such as creating a new note)
* Support note template (snippet is supported)
* Remove `'` and `"` from auot-brackets

## v3.3.0
* Editor: support auto indent, auto list
* Support opening notes with external programs
* Add a delay after code/math blocks update before preview

## v3.2.0
* Support local PlantUml and Graphviz rendering
* Add shortcuts to tab navigation in ViewSplit
* Editor: support auto bracket and overridding font family

## v3.1.0
* Support Japanese translation (Thanks @miurahr)
* MarkdownEditor: guess image suffix when fetching to local
* Refine read mode styles (Thanks @heartnn)
* Support recovering edit session on start
* Support recovering notebook explorer session on start
* Support Flash Page
* Support Quick Access
* Allow to keep docks (like Outline) when expanding content area

## v3.0.1
* Support spell check via Hunspell
* `Ctrl+Alt+F` to trigger full-text search
* Auto focus to the input widget when activating full-text search
* Fix Expand Content Area with panels

## v3.0.0-beta.11
* Full-text search

## v3.0.0-beta.10
* Show more tips
* Add exclude patterns for external nodes
* Add command line parser and support opening files from command line
* Update Mermaid.js to 8.9.1
* Support exporting current note
* Add ExpandAll to node explorer

## v3.0.0-beta.9
* Fix crash when exporting external files
* Support manual sorting folders and notes
* Support showing external files in notebook

## v3.0.0-beta.8
* Note explorer supports different view orders
* Fix `Ctrl+V` paste in editor
* Fix Linux HTTPS crash bug
* Add button to show/hide recycle bin node
* Other small fixes

## v3.0.0-beta.7
* Support export to Markdown/HTML/PDF
* Support base level 1/2/3 for section number in read mode
* Support opening link to folder in read mode

## v3.0.0-beta.6
* Add theme **pure** for light mode
* Small fixes

## v3.0.0-beta.5
* Refine themes
* Add section number style "1.1" and "1.1.", and use the later as default
* Support indentation of first line of paragraph in read mode
* Add file type combo box in NewNoteDialog
* Add "Insert Mark" in tool bar
* Support **Smart Table**
* Support `*.rmd` as Markdown suffix
* Turn on system title bar by default
* Enable AutoBreak by default

## v3.0.0-beta.4
* Add theme **moonlight** for dark mode
* Add shortcuts for split and workspace
* Bug fix
* More configs in Settings dialog for Markdown viewer
* Support section number in edit mode (without the dot suffix)
* Support link jump in Markdown viewer
* Use socket for single instance guard

## v3.0.0-beta.3
* Support `[TOC]`
* Fix MathJax
* Add shortcut for StayOnTop
* Add Markdown guide and shortcuts guide
* Show hovered link in read mode
* Smart Input Method in Vi mode
* Bug fix in Markdown editor when finding current heading index
* Add custom Info.plist on macOS
* Support minimizing to system tray
* Support restart
* Add read and edit in Markdown ViewWindow
* Add Chinese translations

## v3.0.0-beta.2
* Fix Import Legacy Notebook
* Refine toolbar
* Fix Outline viewer
