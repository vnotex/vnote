## v4.13.0
A feature release with outline mind maps, editable node-note previews, source-aware exports and refined editing on top of VNote 4.12.0:

* **Mind maps and navigation**
    * Open source-bound mind-map previews of document outlines, use a sole leading H1 as the central topic, and apply automatic branch colors
    * Focus outline previews on the current heading and click nodes to navigate to their original source headings; reopen the preview after the outline changes
    * Edit mind-map node notes in source-bound plain-text previews with normal autosave; keep these previews attached to their source rather than promoting them to independent tabs
    * Compact node icon rows and fix the clipped Tags label when keyboard shortcut underlines are visible
    * Add Open Link above Copy Link in mind-map URL context menus, including read-only maps
    * Always open context-menu links in a new tab across Markdown, Typst and mind maps, regardless of the new-tab preference
    * Allow Never Ask for local-file and custom-protocol link warnings and share the saved opt-out across warning paths
* **Editing and Markdown**
    * Enable Markdown marker concealment by default and move long-link concealment into the editor menu, with independent persisted preferences and existing opt-outs preserved
    * Fix closing Markdown heading-marker highlighting and preserve syntax backgrounds on the cursor line; update vtextedit and both vendored cmark copies
    * Preserve preformatted and class-hinted HTML code blocks when pasting Markdown, including indentation, language hints and embedded fences, without copying controls or swallowing surrounding prose
    * Update bundled Turndown to 7.2.4 for improved link escaping and ordered-list indentation while retaining VNote's custom paste rules
    * Add Ctrl+W and Ctrl+U editing shortcuts to plain-text fields; assign the default Ctrl+8 and Ctrl+9 shortcuts to unchecked and checked task lists instead of unordered and ordered lists
    * Rename Toggle In-Place Preview to In-Place Preview
* **Export**
    * Make export targets and options follow the selected source types, including native Typst and MindMap source output; reject incompatible selections before creating output
    * Export Typst through its native compiler to HTML and PDF, preserving authored page layouts; copy internal relative images for native source exports and report unbundled imports or dynamic references
    * Add a shared, editable `web/css/export.css` stylesheet for HTML and HTML-based PDF/Custom exports; preserve user styles across upgrades and leave native Typst PDF unchanged
    * Simplify option labels and explain when custom commands use original disk files rather than the current buffer snapshot
* **Notebooks and interface**
    * Support drag-and-drop changes to tag parent hierarchies
    * Replace the node Mark dialog with separate Text and Background color menus using the existing 12 colors and PDF-style swatches; Auto clears only the selected override
    * Keep Chinese IME input in toolbar popup fields on Windows and macOS, with frameless dismissal, overflow access, fullscreen focus and focus restoration after owned dialogs
    * Move the raw-notebook action to the explorer title bar, inset editor status controls within their frame, and throttle tree-column remeasurement during resizing
    * Use `Documents/vnote_notebook` for new first-run notebooks and manually added quick-note schemes without migrating existing folders or saved paths
    * Reopen an existing default notebook during first-run setup without replacing its contents on failure; add `all-in-one.md` and pin it to Quick Access when the list is empty
    * Add the default Quick Note in Current Notebook scheme using `Quick Notes/quick_note_%da%.md`; existing installations are not migrated, and the bundled Chinese welcome note is named `Welcome_zh_CN.md`
    * Expand localized VNote Tips from 12 to 64 with Markdown, navigation, image, PDF, mind-map, export and customization guidance ([source](https://github.com/vnotex/vnote-documentations))
    * Open localized release notes in a new read-only tab after an upgrade, with Simplified Chinese notes and English fallback
* **Document infrastructure and reliability**
    * Support application-owned memory documents in full Text, Markdown, Typst, MindMap, PDF and Image windows with explicit editor types and load/save callbacks
    * Share persistence providers across ordinary, encrypted and memory documents; save memory snapshots asynchronously without clearing newer edits, keep them session-only, and gate file-only actions by capabilities
    * Render rootless Typst and byte-backed PDF documents without temporary document files
    * Improve native CI focus, fixture readiness and consent handling, fix plain-text test linking, and partition Typst coverage without dropping cases or extending test deadlines
    * Make bundled editor tests follow `VNOTE_BUILD_TESTS`, keep the retained-GIF snapshot test opt-in locally and enabled in CI, and allow more time for macOS package WebEngine shutdown
* **Translations**: Simplified Chinese (zh_CN), Japanese (ja) and Korean (ko) catalogs updated
