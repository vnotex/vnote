## v4.14.1
A refinement release with source-linked outline editing, steadier Markdown previews and smoother navigation on top of VNote 4.14.0:

* **Outlines and mind maps**
    * Edit Markdown and safely mapped Typst source headings through toolbar mind-map previews: rename topics, add children or siblings, and move sections with confirmation and paired Undo/Redo
    * Apply accepted edits directly to the source and reuse its Auto Save, Backup File or None policy; closing a rejected topic draft offers Discard or Cancel without discarding changes already applied to the source
    * Keep read-only sources, PDF outlines and unsupported mappings navigation-only; restrict generated maps to basic topic and structure editing, and require reopening after independent source changes
    * Extend Outline panel and popup reordering to ordinary mind maps and safely mapped Typst sections, preserving hierarchy, section content and undo history
    * Keep outline mind-map previews open and focused when navigating to source headings, avoid duplicate section numbers, and preserve newly added topics while source updates settle
* **Editing and Markdown**
    * Keep concealed Markdown formatting markers hidden while editing, with marker-skipping cursor movement; preserve link reveal-on-entry and the existing concealment toggles
    * Ask once when opening a Markdown view whether to conceal or show Markdown markers, remember the choice, and point to the toolbar menu for later changes
    * Update vtextedit to improve folded source headers and preview placement, preserve the viewport during passive layout and unfolding, and stabilize Markdown concealment during IME composition
    * Prevent in-place preview staging flashes in the Markdown reader and fix reader navigation restoration and view teardown lifetime
* **Interface and reliability**
    * Move United Entry's Expand All preference into its own Settings page while retaining the saved choice
    * Select VNote Tips using the resolved application language rather than the regional locale
    * Keep the existing theme overflow indicators readable on narrow view-window toolbars instead of clipping them to a gray square
    * Attach document views before showing detached windows to avoid an empty first frame
    * Update vxcore with desktop-validated mobile-branch integration, including trusted-root alias handling and private long-path fixture fixes
    * Fix widget CI linkage by testing production view-window implementations
* **Translations**: Simplified Chinese (zh_CN) and Japanese (ja) catalogs updated
