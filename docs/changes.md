## v4.14.0
A feature release with scoped HTTP automation, richer Markdown paste and smoother navigation on top of VNote 4.13.0:

* **HTTP automation and security**
    * Add an opt-in, loopback-only HTTP API for local agents and administrator-managed HTTPS reverse proxies, with notebook-scoped tokens and separate read, live-read, write and trash permissions
    * List notes, read saved or live content, search, and conditionally create, edit, append, rename or trash notes without overwriting dirty editors or exposing protected notes
    * Require reversible trash without falling back to permanent deletion, preserve save/sync coordination, and drain pending operations during shutdown
    * Manage tokens in Settings with a dedicated issuance dialog, notebook and permission selection, expiry presets or Never, a 90-day default, one-time Copy and immediate revocation
    * Store listener settings in local session configuration and Windows token verifiers in the local profile, preserving portable isolation and owner-only access, including elevated accounts
    * Audit requests, rejected connections and asynchronous mutation outcomes with privacy-safe structured records and shared request IDs; omit note paths and raw exception details from create/rename diagnostics
* **Editing and Markdown**
    * Preserve KaTeX and MathML formulas when parsing HTML to Markdown and pasting: prefer annotated TeX, recover supported MathML when annotations are stripped, and retain inline/display math and labeled underbraces without duplicate rendered text
    * Avoid unnecessary escapes in leading numbered headings while preserving literal backslashes and paragraph list protection
    * Prevent concealed Markdown markers from blinking during unrelated typing by retaining and remapping the last parsed concealment until fresh results arrive
    * Add Collapse and Expand below Export in read-mode code-block context menus
    * Restore warm Markdown page visibility before rendering when switching tabs, avoiding blank WebEngine frames without keeping inactive pages active
* **Navigation and mind maps**
    * Search file names across notebooks with ordinary United Entry input; require the explicit `>` prefix for commands, show command discovery for empty input, and enable Expand All by default while retaining saved preferences
    * Restore focus to the source content after closing previews, including the mind-map canvas
    * Refine mind-map field menus, shortcut editors and node-anchored popups, improve node icons, and place the nonempty note indicator before the hyperlink indicator
    * Open files classified as Others with the system application instead of the plaintext editor, retain inferred text/image viewers, and skip unsupported restored tabs without launching external applications
* **Interface and reliability**
    * Enlarge Sort Folders and Sort Notes to 800 × 600 by default and select the triggering folder or note when present
    * Reserve persistent editor status rows below the content and prevent Windows status/link-preview overlays from promoting entire tab containers to native windows
    * Stack notification action buttons vertically, unify Settings card surfaces and action alignment, restore complete theme button borders, and clarify editor toolbar option hints
    * Select bundled release-note language from the loaded UI translation rather than the regional locale
    * Improve native CI credential, popup and notification-layout checks, preserve first-failure diagnostics, and recover transient macOS package-mount and WebEngine-shutdown failures
* **Translations**: Simplified Chinese (zh_CN), Japanese (ja) and Korean (ko) catalogs updated
