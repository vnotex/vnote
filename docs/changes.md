## v4.15.0
A feature release with a native TODO planner, dashboard task management and refined editing and updates on top of VNote 4.14.4:

* **TODO planner**
    * Add a native, Markdown-backed `.todo` editor with a Tasks-only interface, full-width layout and automatic saving independent of the global save policy
    * Organize lists under heading-based groups, keep Default-owned lists at the top level, and support list and group dragging, duplication, descriptions and per-list emoji icons
    * Plan tasks with My Day, Important and Planned views, search and tag filters, due dates, Markdown notes and nested checklist steps
    * Edit completion, importance and My Day membership inline; use task context menus and keyboard shortcuts for common actions, dates and moving tasks between lists
    * Sort lists by importance, due date, My Day, name or creation date through list menus, with an option to return to manual order
    * Refine task details, circular checkmarks, completed-step strikethrough, count badges and collapsible Completed sections; keep sidebar navigation accessible in narrow windows
    * Archive completed tasks into sibling `.archived.todo` files while retaining notes, steps and metadata; preserve source groups and lists, and report incomplete moves without discarding the archive
* **Dashboard**
    * Add a TODOs sticker to fresh and reset dashboards, preserving existing layouts; select real lists or My Day, Important and Planned views
    * Add quick task entry, calendar-selected due-date filtering, completion and importance controls, and a collapsible Completed section shared with the TODO editor
    * Open a sticker task on double-click or Enter without navigating away when selecting it or toggling its controls; provide icon-only Edit and Settings actions
    * Remember the sticker's backing file in session configuration and create missing TODO files without overwriting existing contents
* **Editing and Markdown**
    * Add the shared formatting toolbar and configurable editing shortcuts to editable embedded Markdown editors, including TODO task notes, and fix toolbar overflow styling
    * Insert native Markdown markers with InsertMark and recover MathML formulas with simultaneous lower and upper bounds
    * Exit block quotes after two consecutive empty quote lines and insert new lines after entire collapsed blocks, including VSCode and Vi input modes
    * Default Markdown Auto Section Numbering to off for new or missing settings while preserving saved preferences and the Outline panel default
    * Hide code-block Collapse and Expand context actions while text is selected
* **Interface**
    * Scroll overflowing view tabs with mouse-wheel input without switching the active document
    * Keep shared count badges visible at the right edge of explorer trees; load directly tagged note counts lazily while preserving selection
    * Match web scrollbar colors and thumb widths to native widgets, and use active theme colors for editor status chips
* **Windows updates and reliability**
    * Offer the explicit Windows Update Now action in the manual-check dialog while keeping ordinary update checks check-only
    * Display source-appropriate release notes and show bundled notes when VNote is already up to date
    * Offer cleanup of retained Windows update backups after the user confirms the new installation works
    * Fix false image asset publication errors on Windows shared folders without file identity support, retaining no-overwrite and reparse-point checks
* **Translations**: Simplified Chinese (zh_CN) and Japanese (ja) catalogs updated
