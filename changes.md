# Changes
## v4.12.0
A feature release with editable link previews, native Markdown rendering, Typst math and stronger security on top of VNote 4.11.0:

* **Link previews and navigation**
    * Preview supported file links in editable overlays without leaving the current note; use Open Link to open normally and dismiss previews from the source backdrop
    * Open preview tabs in their source workspace without reopening editors or losing drafts and Undo history; replacing duplicate tabs honors Save, Discard and Cancel
    * Require Ctrl+click to open links in the Markdown editor so ordinary clicks only position the cursor
    * Put Open Link before Copy Link in Markdown reader link menus and hide unrelated Edit and Export actions on links and images
    * Allow file-open settings to target an existing main or detached split, preserving background selection and reporting unavailable destinations without rerouting
* **Markdown and Typst**
    * Render Markdown natively with cmark in Read mode, live previews and WebEngine exports, reusing current editor parse trees while preserving wikilinks, tasks, alerts and the table of contents
    * Remove emoji shortcode and emoticon expansion; authored Unicode emoji remain unchanged
    * Add Typst as an optional Markdown math renderer, including authorized encrypted notes, in-place previews, self-contained HTML/PDF exports and cross-copy; KaTeX remains the default, and selecting Typst uses Typst syntax inside the existing dollar delimiters
    * Render fenced `typst` blocks as self-contained markup independently of the math setting, preserving authored page layouts and multi-page output in previews, exports and cross-copy
    * Align Markdown Typst blocks with surrounding content by removing their default page margin; explicit margins and standalone document layouts remain unchanged
    * Replace the Typst helper process with a bundled shared runtime and bounded background workers, retaining previews and exports with orderly shutdown
    * Discover installed system fonts to fix missing Chinese glyphs in Typst previews and exports, while retaining configured-font precedence
    * Match Typst editor fonts and preview backgrounds to the active theme, including Typst-specific overrides; authored document styles take precedence and theme preview colors do not alter exports
    * Remove informational overlays from the Typst PDF preview while retaining errors and compiler diagnostics; clean obsolete Typst images when document closure is accepted
    * Keep presentation requests alive across delayed reader focus and native window reconstruction, and prevent stale PDF state or WebChannel rebinding from disrupting replacement notes
    * Restrict editor section numbering to ATX headings rather than numbering Setext headings
* **Security and tasks**
    * Fix Markdown raw-HTML URL sanitizer bypasses by validating browser-decoded URLs before installing content in readers, live previews and exports
    * Add a searchable Protect Markdown from XSS setting under Markdown Editor → Read; filtering remains mandatory for encrypted notes
    * Prevent task command injection through filenames, selected text and other variables: shell commands and options must be static, variable values belong in literal `args`, and values are no longer recursively expanded
    * Migrate the bundled Git commit task and provide guidance for custom tasks; shell tasks with arguments require Bash or PowerShell, otherwise use `type: process`
    * Share external-link warnings and the Never Ask preference across Markdown, Typst and mind maps while preserving authorization, cancellation, stale-view checks and independent local-file warnings
* **Mind maps and interface**
    * Add image context actions to view, copy, copy the source address and resize with Undo support; hide image tooltips that exposed URLs or base64 data
    * Apply mind-map node-properties placement immediately without reopening the editor or losing edits; keep properties embedded in detached windows without changing the main-window preference
    * Activate detached windows and focus their editors after moving a tab
    * Make the search panel scrollable, fix wrapped notification layouts and correct Mermaid label wrapping at fractional display scaling
    * Remove native title bars from toolbar popups on macOS and dismiss them with an outside click or Escape, matching Windows
    * Theme in-editor table borders and link-preview buttons consistently, refreshing them on theme changes; use the shared pilcrow heading-link symbol in Lucius
* **Synchronization and compatibility**
    * Allow HTTP Git notebook synchronization alongside HTTPS and file URLs; HTTPS is recommended because HTTP sends credentials and data in plaintext
    * Refresh bundled KSyntaxHighlighting 6.30.0 resources with 464 syntax definitions and 31 highlighting themes; use Catppuccin Latte as the default light syntax theme while retaining UI theme-specific overrides
    * Keep shared Markdown preview themes current across light/dark switches, including edit-only tabs, so math retains the correct foreground
    * Trim unused macOS package dependencies and verify signed packages on native Apple Silicon and Intel runners before publication; retain both macOS package variants
    * Verify Linux AppImages with their shipped xcb platform, fix Windows package staging and standalone mind-map test linking, and improve native test input and diagnostic-report preservation
* **Translations**: Simplified Chinese (zh_CN), Japanese (ja) and Korean (ko) catalogs updated

## v4.11.0
A feature release with native Typst editing, reusable note tabs and refined mind-map tools on top of VNote 4.10.0:

* **Typst**
    * Create and edit `.typst` notes with a bundled native compiler, live PDF preview, in-place previews and compiler diagnostics; switch to PDF-only Read mode without losing the source editing session
    * Toggle Live Preview and In-Place Preview independently, search source in Edit mode and PDF text in Read mode, and use page navigation, presentation and shared zoom controls
    * Follow the active theme in the preview, with a per-view Theme Style in Preview toggle; explicit document styles take precedence and exports retain their original styling
    * Insert images, links, attachments and file contents from dialogs, the clipboard and drag-and-drop, keeping local image resources inside the note's Typst project
    * View, copy and safely resize images from source, in-place previews and PDF context menus; open image overlays with zoom and pan, and route local-file links through VNote
    * Navigate compiled headings from Outline without selecting their text, use consistent preview menus in Edit and Read modes, and restore the session cursor without jumping to the end
    * Add a default `title.typst` template using the filename without its extension, while preserving existing template choices
* **Tabs and performance**
    * Add an Always open notes in new tabs setting; disable it to reuse the current tab, or hold Ctrl when activating a note to open a new tab
    * Retain compatible editors, preview resources and zoom/layout preferences across note replacement, with save/comment confirmation and isolation of document history and pending operations
    * Release hidden Markdown web views after a configurable timeout, defaulting to five minutes; returning reloads the current preview without losing unsaved text or Undo history
    * Delay Markdown readers in edit-only mode, share editor JavaScript work across tabs with protected-note isolation, and release obsolete MathJax, menu and Graphviz content when readers change
* **Mind maps**
    * Refine balanced layouts with clockwise ordering, add Ctrl+H/J/K/L navigation, show branch-facing expand/collapse controls on hover, and restore canvas keyboard focus after layout changes
    * Use softer theme-derived connectors, outlines and tag borders in the editor and exports; match properties controls and separators to the active theme
    * Exclude neutral colors and Mint from automatic branch colors while preserving existing branch colors and manual choices
    * Add a default `title.mmm` template using the note title as the central topic, while preserving existing template choices
    * Add Alt+M for the Image property while keeping Alt+I for Icons, align property heading styles, and remove the redundant add-link shortcut and dialog
* **Printing and export**
    * Print mind maps from the toolbar menu, fitting the current layout onto one page with vector text, images, links and inline drafts; open Export directly from the empty-canvas context menu
    * Print the rendered Markdown document in Read mode and current source in Edit mode, including selections and live-preview split layouts
    * Produce readable document-style mind-map Markdown exports and align node links with their HTML articles
    * Fix clipped KaTeX fractions in PDF/custom exports and inline math alignment in exports and cross-copy (#2776)
    * Initialize export rendering and syntax styles from the current theme on the first dialog show of each session, retain later choices, and show the notebook name in Current notebook exports
* **Search, themes and interface**
    * Add United Entry aliases `t`/`T` for tag searches in the current/all notebooks, and `N`/`G` for filename/content searches across all notebooks; group aliases by scope
    * Add Undo and Redo toolbar buttons to text and Markdown editors, show MARKDOWN in the editor status widget, and give document-tab close icons more space (#2775)
    * Float and round editor status surfaces, move image overlay controls beside Close, and shorten tip notifications to ten seconds
    * Fix note drag-and-drop reordering with external files visible and preserve expanded folders
    * Match PDF margins and native context-menu icons to the active theme; restore Native palette behavior and use Everforest red for headings and green for inline code
    * Preserve safe custom Markdown anchor IDs without requiring a prefix, and remember Never Ask for external-link warnings while retaining local-file and protected-note safeguards (#2773)
* **Compatibility and builds**
    * Require Qt 6.9+ and remove Qt 5.15 and Windows 7 support; bundle the native Typst runtime and its license notices, and trim unused Windows NMEA dependencies while rejecting obsolete application-local UCRT files
    * Update syntax highlighting to KSyntaxHighlighting 6.30 with native Markdown code-block highlighting by default, including Typst syntax support and theme/preview fixes; remove unused editor-color sections without changing token styles
    * Require CMake 3.21+, Python 3 and a C++20-capable compiler for the syntax indexer; VNote remains C++17 and normal syntax builds no longer require Perl
* **Translations**: Simplified Chinese (zh_CN), Japanese (ja) and Korean (ko) catalogs updated, including Typst, tab reuse, mind-map Paste and emoji categories; refine Chinese and Korean interface wording

## v4.10.0
A feature release with richer mind-map editing and exports, image viewing and self-contained encrypted notes on top of VNote 4.9.1:

* **Mind maps**
    * Add independent trees, full-branch duplication, and native node Copy/Paste with fresh IDs, preserved properties and internal links, visible selection and one-step undo
    * Add recursive Expand All and Collapse All to node context menus, including in read-only maps; put Copy/Paste directly below node creation
    * Add a layout menu and remember the default layout; support automatic section numbering with the shared editor numbering pattern
    * Insert images and hyperlinks from the clipboard, drag-and-drop, toolbar and context menu through shared dialogs; preserve existing topics for links and insert multi-item batches as one undoable command
    * Add icon and tag inputs to node context menus and Copy Link for node URLs; hide the unused Title field in mind-map image insertion dialogs
    * Open inline editing by double-clicking a topic label or blank node space, including empty topics, without interfering with images or expand/collapse controls
    * Honor rapid repeated expand/collapse clicks and keep hover feedback under a stationary pointer, including in read-only maps
    * Prioritize 173 curated productivity, project, diary, office and engineering emoji, including numbered keycaps 0-10, while retaining the full Unicode catalog and category order; add search keywords and theme-aware selection, hover and keyboard-target highlights
    * Make Shortcuts Help available from node and empty-space context menus, show its configured shortcut, and clarify the overlay instructions with bold key bindings
* **Export**
    * Export mind maps as readable Markdown, native HTML with selectable node text and inline SVG connectors, or PDF; include independent trees and preserve peer-root heading structure
    * HTML presents the full-width map at its natural size with topic links to continuous articles containing notes, images, icons, tags and links; PDF fits the map on the first page and keeps articles readable on subsequent pages
    * Use the live editor snapshot for Current Buffer exports and disk contents for Current Note and batch exports; wait for PDF images and fonts before printing and report rendering, timeout and output failures
* **Images and Markdown**
    * Open images in read-only tabs and expose image file associations in Settings
    * View Markdown and mind-map images in overlays with zoom, pan and reset-to-fit controls; support read-only maps and close protected image previews when notes are locked
    * Embed local images in WeChat and Evernote cross-copy, rasterize SVG graphs and MathJax, and render complete KaTeX formulas with embedded fonts and clear high-DPI output
    * Preserve the edit cursor and selection when returning from Markdown read mode if the mapped position is already visible (#2768); add Select All to the read-mode context menu
* **Encrypted notes**
    * Make newly encrypted notes and backups self-contained: each v2 `.vne` carries its wrapped keys instead of depending on a shared notebook vault
    * Migrate legacy notes and backups on writable open without rewriting encrypted payloads; read-only opens leave files unchanged, shared master-password unlocking remains available, and old vaults are retained for unmigrated notes and historical copies
    * **Older VNote versions cannot read v2 encrypted notes**
    * Fix encrypted-note opens silently failing during notebook maintenance; self-contained notes no longer need a migration lease, and protected-open errors reach the unlock UI
* **Interface and reliability**
    * Remember Markdown read zoom, shared text-editor zoom and mind-map fixed or Fit zoom in session state; remove redundant Settings zoom controls and show whole percentages without reducing actual mind-map zoom precision
    * Remember Comments and Tools dock sizes across editor switches and restarts, and use icon-only View and Settings menus in the main toolbar
    * Closing a detached window closes its tabs instead of reattaching them to the main window
    * Show note sizes in Properties using human-friendly units
    * Remove obsolete attachment entries during Scan after external renames or deletions, even when no new files are found (#2771), and restore the Scan tooltip
    * Keep Lucius tree selections distinct from hover highlights when unfocused and preserve text-selection visibility while hovering inputs and spin boxes
    * Add testing-only `--no-single-instance` to bypass the instance lock and IPC; use a separate disposable portable profile and test data because configuration and notebooks are not isolated
    * Fix libgit2 runtime consistency and encrypted-note, sync-dialog and mind-map integration regression fixtures
* **Translations**: Simplified Chinese (zh_CN), Japanese (ja) and Korean (ko) catalogs updated

## v4.9.1
A maintenance release with more flexible mind-map tools, theme fixes and attachment improvements on top of VNote 4.9.0:

* **Mind maps**
    * Show node properties in a Tools dock by default, with a setting to keep them embedded in the editor; dock titles follow the active view and property edits remain synchronized when focus moves to the dock
    * Keep folding local to each editor and preserve node positions when folding and expanding branches
    * Standardize English mind map wording in settings and documentation
* **Markdown and themes**
    * Refresh cached code-block and math syntax colors when switching themes without reopening notes or losing drafts, selection or Undo/Redo history
    * Restore code-block syntax highlighting in encrypted Markdown edit mode and apply bundled reader and syntax styles to encrypted previews on opening and theme changes, including dark themes
* **Attachments and encrypted notes**
    * Fix Chinese input when renaming attachments on Windows; after a successful rename, reopen the attachment list with the renamed file selected and focused (#2755)
    * Fix encryption and decryption of open notes incorrectly treating their temporary editing freeze as a read-only change
    * Add privacy-safe note-conversion diagnostics identifying failed checkpoints and backend phases
* **Translations**: Simplified Chinese (zh_CN), Japanese (ja) and Korean (ko) catalogs updated; restore missing translations in Sort/Properties dialogs, locked notes, counts, settings, command-line help and exported HTML link prompts

## v4.9.0
A feature release with WebDAV synchronization, a native mind-map editor and opt-in Windows updates on top of VNote 4.8.0:

* **Notebook synchronization**
    * Sync notebooks through WebDAV with ordinary editable remote files, secure per-device credentials, remote open, conflict choices, cancellation and journaled recovery
    * Generic WebDAV and Nextcloud use dedicated existing collections with conditional writes; HTTPS is recommended because HTTP exposes credentials and notebook data, and empty remote folders are retained rather than recursively deleted
    * Add Jianguoyun sync with VNote-managed versioned remote storage, remote clone, whole-version conflicts and resumable recovery; retained remote history and deleted data are not automatically removed
    * Preserve encrypted key/file cohorts and open editor/comment drafts during sync, retain separate provider recovery archives, and refresh clean editors, previews and notebook trees after incoming changes
    * Show underlying setup failures in New Notebook, including failed WebDAV checks, HTTP status and missing or weak ETags; display the sync method below notebook type and keep it selectable in Manage Notebooks
* **Mind maps**
    * Replace the web-based mind-map view with the native m3 editor, including theme-aware formatting controls, Undo and Redo, links, images, search and an outline
    * Use `.mmm` as the preferred built-in suffix while retaining `.emind` support and existing custom suffix settings
    * Add persistent settings for default fonts, text size, undo history, subtree-deletion confirmation and automatic branch colors; applying settings updates open maps without reloading them
    * Open and reload maps at 100% zoom; toolbar zoom controls stay synchronized with shortcuts and Ctrl+wheel, offer Fit and percentage presets, and accept custom percentages with Enter
    * Present mind maps fullscreen with F9 and exit with Escape; node properties start collapsed and the toolbar fades like text-note presentations
    * Empty mind-map files open with a Central topic ready to edit, and saving no longer closes an active inline edit
* **Markdown and images**
    * Preview and format tables immediately after paragraph or list-item text without requiring a blank line (#2763)
    * Skip empty headings when generating editor section numbers; turning Allow Auto Section Number off restores only source prefixes changed in the current editor session, preserving manual numbering
    * Put image actions directly in the context menu, align View Image (V) and Copy Image (G) access keys in read and edit modes, and add proportional percentage resizing to Set Image Size
    * Support Shift+wheel horizontal scrolling in the text editor
* **Themes and interface**
    * Restyle Dreamy with a Phycat Mauve-inspired light palette, plum accents, rounded gradient headings, coordinated editor and reader syntax, and matching control icons
    * Use system text colors for Native theme content, including mind maps, and remove the Lucius Markdown read-mode width cap while preserving responsive padding
    * Localized theme names follow UI-language preferences for Default language, including Chinese script tags; add Simplified Chinese names for Dreamy, Jadeite and Pink Shock
    * Use text-only View and Settings menus on the main toolbar, move Themes under Settings, and rename the window submenu to Windows List
    * Hide Comments for unsupported views, collapse empty editor status rows, and size notification popups to their content
    * Copy filesystem paths with native separators in notebook, attachment, tag and tab actions (#2762)
* **Encrypted notes and reliability**
    * Record notebook encryption initialization explicitly instead of scanning all folder metadata; legacy notebooks with unknown encryption history require confirmation before first setup
    * Note conversion no longer audits unrelated folder metadata; missing known keys still require recovery, and cancelling legacy confirmation leaves notes and configuration unchanged
    * Fix a use-after-free when a notebook closes after its explorer widget is destroyed, and improve delivery of files opened through a secondary VNote instance
* **Updates and packaging**
    * Add an explicit Windows Update Now notification action using an external updater; it verifies signed full packages before requesting shutdown, preserves portable configuration and unowned files, and retains a recovery backup
    * Ordinary update checks remain check-only, with Check Release available; protected installations receive manual-update guidance rather than elevation, and reopening follows an accepted shutdown
    * Add a separate universal macOS package targeting 12.1 alongside the macOS 13+ package; use Schannel for Git sync in Windows 7 builds
* **Translations**: Simplified Chinese (zh_CN) and Japanese (ja) catalogs updated, including the native mind-map editor; add Korean interface translations for VNote, text and mind-map editors, startup tips, Git tasks and bundled Qt dialogs

## v4.8.0
A feature release with Markdown presentations, search-and-replace across notes, new light themes and more flexible encrypted notes on top of VNote 4.7.0:

* **Presentations and Markdown**
    * Present rendered Markdown as offline reveal.js slides with F9; H2/H3 headings split slides, H3 slides retain their parent H2, and oversized content scrolls
    * Presentation palettes follow the active VNote theme, including Native; PDF presentations retain their controls in whole-view fullscreen and refit when the viewport changes
    * A default-on, per-window Allow Auto Section Number toggle temporarily disables numbering in Markdown read mode, edit mode and the outline without changing saved settings
    * Choose whether a sole leading level-1 heading is excluded from automatic section numbering or numbering starts at the first heading
    * Select JPEG (default) or PNG with transparency for clipboard images saved as files; original file/URL formats and lossless Base64 insertion remain unchanged
    * Inline and display math previews are dispatched in document order, and CSS heading-fold arrows no longer paste as oversized SVGs into rich-text editors
* **Search and replace**
    * Replace selected or all reviewed Simple content-search matches with literal text; confirmation saves affected notes, including existing unsaved edits, with cancellation and failed-write recovery
    * Content-search results navigate to the correct editor line
    * Vi-mode search updates incrementally and uses correct source positions in folded documents and table cells
* **Encrypted notes**
    * Decrypt selected encrypted notes in place to return them to normal notes
    * Shared-folder bundle import accepts encrypted mind-map notes while preserving authenticated transfer and destination-key protection
    * Bundled KaTeX works offline in encrypted Markdown reading and live previews; in-place math previews remain blocked
    * Note encryption works when the notebook has an empty Git sync directory; preflight failures include notebook locations and localized guidance for unreadable sync metadata or conflicting key files
* **Export**
    * A compact palette-icon Follow Theme button selects the active theme's rendering and syntax styles
    * HTML exports resolve the selected theme palette like read mode, preserving table borders and other themed colors
    * The exported outline opens and closes smoothly, with refreshed controls, active-section styling, keyboard support and reduced-motion support
    * HTML code blocks retain read mode's Copy and Collapse/Expand toolbar; copying a collapsed block still copies all of its code
    * Exported outline and code-block labels, tooltips and accessibility text follow the application language, including Simplified Chinese and Japanese
* **Themes and interface**
    * Added three light themes: Lucius, based on Jonathan Filip's Lucius Light normal-contrast palette; Dreamy, with light lavender colors; and Jadeite, with linen and teal colors
    * Light-theme graph canvases blend into the page, including PlantUML, Mermaid, Flowchart, WaveDrom and Graphviz SVGs; PlantUML PNG previews are transparent while preserving explicit diagram backgrounds and node fills
    * PDF viewer backgrounds and scrollbars follow the active theme without recoloring PDF pages, and annotation icons refresh on theme changes
    * Outline and Find And Replace stay on view-window toolbars while secondary actions move into a three-dot Menu; PDF Find sits beside Outline and Readable Width is available in the Menu
    * Editor-toolbar Outline, Tags, Attachments, Word Count and Insert Table popups stay anchored to their buttons and on-screen, including in presentation mode
    * Read-only note tabs and notebook badges use a pen-off icon, and History sticker tooltips show full filesystem paths
    * Refined Pink Shock syntax colors, restored the Lucius button border and Dreamy/Jadeite Close hover contrast, and made the United Entry icon follow toolbar colors
    * The startup splash appears only for the primary instance; a 256x256 anniversary splash is shown during October 2026 according to the local date
* **Help and settings**
    * Restored read-only Markdown Guide and Shortcuts Help from the Settings menu in English and Simplified Chinese, with English fallback and an updated keyboard reference
    * The greeting sticker shows hourly tips, and localized tips follow the preferred UI language rather than the system regional format
    * A default-on General setting allows Ctrl+Alt shortcuts; disable it and restart to avoid AltGr conflicts without losing custom bindings
    * A text-only Edit JSON button on the Settings toolbar opens vnotex.json in VNote
* **Reliability and diagnostics**
    * Read-only buffers, including View Logs, survive visible and hidden workspace recovery after restart
    * Freshly installed bundled tasks load on first request without requiring a restart
    * Removed buffer-reload log feedback at normal log levels and added Git sync phase timings, queue outcomes and bounded credential diagnostics, plus Windows Git/WinHTTP diagnostic tooling
    * Fixed sync credential callback lifetimes and shutdown crashes with native keychain operations in flight; repaired Qt 5 presentation builds and cross-platform CI fixtures, keychain event loops and sync IO-gate checks
* **Translations**: Simplified Chinese and Japanese catalogs updated for the new strings

## v4.7.0
A feature release with encrypted notes, richer Markdown editing, offline math rendering and faster keyboard navigation on top of VNote 4.6.1:

* **Encrypted notes**
    * Protect the contents of individual Markdown, plain-text and mind-map notes in bundled notebooks with a password-protected portable key hierarchy
    * Unlock on demand; Lock All in the Settings menu saves protected edits and closes protected views while leaving ordinary tabs open
    * Protected note previews use memory-only profiles; exporting and printing encrypted note bodies are not supported
    * Filenames, folders and tags remain visible; conversion does not erase plaintext in existing Git history, backups, shared resources or external source files
    * Images, attachments and comments stored as separate files remain unencrypted; conversion warns about this and never scans or changes assets
    * Insert as Base64 embeds images through reference links and is the default insertion method for encrypted Markdown notes
* **Editor and Markdown**
    * Automatically number unnumbered headings in read mode and the outline; an independent edit-mode option writes and maintains heading numbers in the source, using the shared section number pattern
    * Navigation mode reaches Markdown content across visible splits, in both edit and read modes
    * The editor includes upstream AST-aware list editing and Vi open-line continuation; themed list-item guides and active-item backgrounds accompany automatic ordered-list renumbering, which can be disabled in Markdown Editor settings
    * Long image, link and reference destinations are visually shortened by default in edit mode; concealment can be disabled without changing the source
    * Markdown pipe-table preview cells display inline images and math; Insert Image targets the selected cell, including embedded images and image-host uploads
    * Single-line `$$...$$` formulas preview in table cells, paragraphs and lists with inline placement and display-style typesetting
    * Editor find supports multiline matches, and the replacement tooltip explains capture references, newline, tab and literal-backslash escapes
    * Bundled notebooks can override the global editor line-ending setting, and saves respect Git line-ending rules
    * Clipboard images inserted as files use JPEG with a white background for transparency; original file/URL formats and lossless Base64 insertion are unchanged
* **Math and diagrams**
    * KaTeX, its fonts and the preview rasterizer are bundled for offline reading, editor previews and exports; standalone HTML embeds the math fonts, while protected-note math restrictions remain unchanged
    * PlantUML sequence diagrams show each `newpage` page in read mode and stack all pages into one editor-preview image, without changing the note source
    * Choose SVG or PNG for PlantUML graphs in read mode and exports; editor previews use PNG, and PNG-only renderers no longer loop through duplicate pages
    * Graphviz accepts engine-named Markdown fences as well as the existing graph syntax
    * Math rendering, in-place previews and raster exports wait for KaTeX fonts rather than document-wide font/layout readiness, avoiding hidden-viewer stalls
    * Invalid PlantUML SVG responses fail locally without replacing the failed block's source, and valid partial pages remain usable
    * Headings containing math formulas retain formula text in their anchors, fixing navigation to math-only headings and keeping TOC links consistent
* **Quick notes and notebooks**
    * Quick Note is available through a global shortcut and the system tray; each scheme can open its notes in a detached window from any quick-note entry point
    * New quick-note schemes default to the same `my_notebook` folder under Documents used for the first-run notebook; existing scheme folders remain unchanged
    * Quick notes outside notebooks open at their actual path; the picker and parentless error dialogs activate even when the main window is hidden or minimized
    * File import from the toolbar uses the correct source and destination folder, selects imported files and reports batch results
    * Folder rename releases Windows directory watches and preserves descendant metadata, loaded child rows, expansion and selection without requiring Reload
    * Sorting from a folder in the notebook explorer reorders its parent's children, matching notes
    * Gitee notebook sync accepts the token owner's Git username; Configure Sync and Open Notebook expose the field, and username-only changes preserve local Git history
* **Interface and packaging**
    * Added the Pink Shock light theme
    * Daily localized usage tips include acknowledgement and a persistent opt-out, and the default notebook includes Markdown guides
    * Save All is available directly above Close Tab in the tab context menu, and notebook context-menu actions have been reordered
    * The Outline dock receives more default space than Comments; search status text no longer widens its dock
    * The notification popup has a taller, font-scaled minimum height; long toasts stay visible for 60 seconds and persistent toasts auto-hide after 30 seconds, above native WebEngine content
    * Git task icons and the Image Host toolbar icon follow live theme switches, and existing math and diagram previews regenerate with the new theme
    * Application views no longer use alternating row backgrounds; selected Location List results remain readable in the VX Idea theme and retain their selection background with the native Windows 11 style
    * Heading hover no longer changes the layout in read mode or exports, and the Markdown read-mode WebEngine inspector is available again
    * Linux AppImages are built on Ubuntu 22.04 for broader distribution compatibility
* **Translations**: Simplified Chinese and Japanese catalogs updated for the new strings

## v4.6.1
A maintenance release with cross-notebook transfers, more flexible Markdown editing and preview fixes on top of VNote 4.6.0:

* **Notebooks and attachments**
    * Copy and move notes and folders between bundled notebooks, with progress and per-item results; pending note edits and comments are saved before copying, while moves require the affected notes to be closed
    * Attachments remain usable after cross-notebook transfers, including notebooks with existing attachment metadata
    * The attachment popup can scan for unlisted attachments and register them in place, and opening an attachment now follows the standard file viewer flow
    * Bundled notebooks can use a custom recycle bin folder, specified as an absolute path or relative to the notebook root
    * Optional automatic recycle bin cleanup permanently removes entries older than a configurable retention period
* **Editor and Markdown**
    * Headings can be folded in read mode and HTML exports, with fold controls shown beside the heading on hover
    * Drag headings in the outline to reorder whole sections, adjusting heading levels to preserve the hierarchy, including moves under an adjacent heading
    * A table-size picker was added to the Markdown toolbar
    * Table-source alignment also formats directly edited Markdown tables; interactive table previews support Markdown type actions, highlight edited cells and write changes back after one second
    * Choose KaTeX or MathJax for formulas in read mode and previews, with room reserved for numbered KaTeX equations
    * KaTeX, MathJax and SVG-based graph previews retain higher-resolution pixels without enlarging their display size; KaTeX raster exports retain the same extra detail
    * Inline and display math previews resize immediately with editor zoom while sharper rasters refresh asynchronously, preserving unrounded layout geometry
    * Folding ranges track surviving source anchors after edits; bulk deletion no longer retains deleted block handles, preventing folding crashes, misplaced fold markers and hidden unrelated text
    * The `%folder%` magic word expands to the current note's notebook-relative folder in snippets and note templates
* **Export and interface**
    * Export is available directly from the main file toolbar
    * Exported HTML offers the corresponding HTML target when following a relative Markdown link
    * The Save shortcut applies changes in Settings
    * The United Entry toolbar input has a bounded width, and its `n` alias searches files by name
    * No-match find feedback, panel close buttons and notification close icons follow the active theme
* **Fixes**
    * PDF highlights, drawings and text boxes use the page content origin, eliminating the page-border offset when capturing annotations
    * PDF highlights join adjacent text fragments into one region per continuous line, removing internal frames and overlapping fills, including for existing highlights
    * Qt 6.9+ builds no longer create an empty WebEngine directory under Roaming AppData/VNoteX on Windows
    * Cloning notebooks and enabling Git sync no longer crash on Linux due to unintended libgit2 Iconv configuration
    * Hovering Markdown links no longer resizes the editor, and creating a Markdown style file no longer produces a spurious warning
* **Translations**: Simplified Chinese and Japanese catalogs updated for the new strings

## v4.6.0
A feature release that adds PDF comments, batch tag editing and substantial Markdown preview improvements on top of VNote 4.5.0:

* **PDF viewer and comments**
    * The bundled pdf.js viewer was upgraded to v6 and now supports per-file PDF comments: draw, highlight and place movable text boxes, with per-tool colour, size and opacity settings
    * The built-in web toolbar was replaced by native Qt controls, including an overflow menu and a dedicated comments panel
    * The built-in PDF viewer is disabled on the Qt 5 / Windows 7 build because pdf.js v6 requires a newer web engine; PDFs continue to open with the configured external application
* **Editor and Markdown**
    * Task-list checkboxes in the read-mode preview are clickable
    * The edit-mode outline shows rendered heading titles, and image sizes can be authored from the image context menu
    * New settings align Markdown table source and automatically fold previewed blocks
    * Interactive table previews are built on demand, preserving space for off-screen tables while substantially reducing opening, editing and scrolling costs; syntax highlighting is capped at 5000 cells in extreme documents
    * Read-mode diagrams appear progressively, duplicate Mermaid renders share one result, and repeated PlantUML diagrams reuse their encoded server URL
    * In-place previews publish results in batches, prioritize visible graph previews and recover cleanly when a render pass stalls, is hidden or is replaced
    * Notes with more than 100 math blocks are no longer silently truncated, image-host images are not downloaded again for each preview, and multiline inline highlights render correctly
    * Prism was upgraded to 1.30.0 with every bundled language
* **Tags**: tags can be added to or removed from multiple selected files in one operation
* **Interface**
    * The right dock tabs now use the same compact sidebar treatment as the left dock, with consistent indentation and theme-owned row spacing
    * The sort dialog shows Name, Created and Modified columns and compares wide digit runs as fields
    * VNote can start automatically when signing in to Windows
    * The application display name used in window titles, the system tray and About can be customized
    * Windows spanning screens expand on the correct display, notification popups no longer truncate horizontally, and the notebook context menu places **Close** at the end
* **Fixes**
    * Opening a file through Windows **Open with** no longer crashes when the WebEngine process inherits an incompatible command line
    * Read-only state is resolved when a buffer opens, preventing stale permissions from leaking into later operations
    * Context menus use the correct Qt API on each supported major version
* **Translations**: Simplified Chinese and Japanese catalogs updated for the new strings

## v4.5.0
A feature release that adds shared-folder import, two paper themes, movable dashboard stickers and a batch of export fixes on top of VNote 4.4.3:

* **Import a shared folder** (Notebook explorer › Import Folder): the other half of 4.4.3's **Share Folder**
    * A bundle produced by Share Folder is imported with its notes' ids, dates, tags and attachments intact, so a folder can travel between notebooks without losing its history; importing an ordinary folder from disk still generates fresh ids and timestamps
    * The bundle is validated before anything is copied, the copy is verified before it is published, and a cancelled or failed import leaves the notebook exactly as it was
    * Refuses symbolic links, junctions and reparse points, detects duplicate node ids and names that would collide on a case-insensitive filesystem, and declines to re-import a folder that is already in the notebook rather than overwriting it
* **Two new themes: LaTeX Light and LaTeX Dark.** A paper-and-ink look for the Markdown viewer, editor and export — serif body text with a capped line measure, weight-led headings, booktabs-style tables (no vertical rules, no zebra stripes) and a boxed blockquote. No fonts are bundled: the stylesheets fall back to Times/Georgia and Consolas, and installing Latin Modern (plus Noto Serif CJK for Chinese) gives the intended look. Inspired by [typora-latex-theme](https://github.com/Keldos-Li/typora-latex-theme); independently authored, with nothing copied from it.
* **Editor**
    * The cursor position is now restored when a buffer is reloaded from disk
    * In-place previews (images, diagrams, formulas) zoom together with the editor
    * The source of a previewed fenced-code, display-math or table block is folded automatically when its preview appears; unfolding it by hand is respected
    * The interactive table in-place preview is enabled by default: tables can be edited in place as a real rich-text sheet, with syntax-highlighted cells, row/column/alignment operations from its Table menu, and Enter in the last cell appending a row; its preview sheet no longer draws a stray frame and background
    * Block quotes continue automatically on Enter, including nested and lazily-continued ones
    * The default input mode for the text editor is now **VSCode**, where `Ctrl+Shift+[` and `Ctrl+Shift+]` fold and unfold at the cursor
    * The drag-and-drop cursor is shown at the drop target instead of at the pointer (#2249)
    * Snippet date placeholders `%MMM%`, `%MMMM%`, `%ddd%` and `%dddd%` follow VNote's language instead of the system locale
* **Notes & templates**
    * **New Note** proposes an available default name, and remembers a default template per file type
    * A built-in `title.md` template ships out of the box
    * **Open as Detached** was added to the notebook explorer context menu
    * **Open V3 Notebook** moved into the Open Notebook dialog
* **Export**
    * Mermaid diagrams are rasterized at the printed size and 384 dpi, so they are no longer blurry in PDFs
    * CJK text and Mermaid labels render correctly in wkhtmltopdf PDFs, with a clear warning when no CJK-capable font can be loaded
    * A file opened from outside any notebook can be exported
    * wkhtmltopdf process failures are reported instead of failing silently, wkhtmltopdf-only PDF options are disabled when it is not used, and the **Complete page** option has an explanatory tooltip
    * **Export** was added to the Markdown read-mode context menu
* **Dashboard**: stickers can be moved and resized by direct manipulation — drag the centre to move or an edge to resize, with arrow keys (and Shift with arrow keys) as the keyboard equivalent
* **Interface**
    * The notebook tree scrolls horizontally when labels overflow
    * The dock separator is more visible across themes
    * The Home tab closes automatically when a file is opened
    * The main window's fixed 800x600 minimum size was removed, and the PDF navigation pane is hidden by default
    * Dialog form labels dropped their trailing colons
* **Fixes**
    * Image hosts are persisted when the settings are saved, and remote images that are still referenced elsewhere are no longer deleted
    * A cleared quick access list is persisted, Reset no longer leaves a stale selection on the Appearance page, reloading File Associations no longer keeps a stale Add Program button pointer, and reloading image-host settings while a test is pending no longer risks a crash
    * Note move failures report the underlying vxcore error detail (#2729)
* **Windows 7 build (`win64-windows7`) fixes**
    * **Search now works.** Content, file and tag searches silently returned nothing on this build: every search result was discarded before it could reach the UI. The same defect also stopped image-host uploads from ever starting.
    * **Network/HTTPS now works.** Checking for updates and uploading to an image host failed with "TLS initialization failed" because the bundled OpenSSL could not be loaded. VNote now ships an OpenSSL that actually loads (1.1.1w, built from source), together with its licence notice, and logs a clear message if TLS is ever unavailable.
* **Build**: Qt was bumped to 6.10.3 on all platforms, tests are no longer built by default (`VNOTE_BUILD_TESTS`), and a regression gate catches drift between the two vendored cmark pins
* **Translations**: Simplified Chinese and Japanese catalogs updated for the new strings

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
* Mind Map: add outline and linking support
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
