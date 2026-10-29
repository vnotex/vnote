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
