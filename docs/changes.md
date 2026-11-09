## v4.14.2
A refinement release with safer notebook tasks, Windows integration and smoother editing on top of VNote 4.14.1:

* **Notebook tasks and security**
    * Require local notebook trust before running notebook tasks or evaluating their variables; remember approval across restarts, forget it on explicit close, and automatically trust only newly created local notebooks
* **Editing and Markdown**
    * Replace the two Markdown concealment toggles with None, Passive and Aggressive presets; default to Aggressive without migrating the old settings, map the editing-experience choices to Aggressive/Passive, and use Aggressive in embedded Markdown editors
    * Update vtextedit to support longer compact URL destinations and keep insertion carets inside concealed closing markers while typing
    * Recover named functions, limits, upright identifiers and supported spacing when pasting browser MathML without TeX annotations, including sinc formulas
* **Windows integration and updates**
    * Add per-user Read in VNote and Edit in VNote context-menu actions for applied built-in file suffixes through Settings → File Associations; open reading in a detached window and editing in the main window, preserving explicit modes through startup and single-instance forwarding
    * Reconcile changed context-menu suffixes and remove legacy all-files entries without requiring administrator privileges
    * Check GitHub and Gitee releases without REST APIs; fetch explicitly requested GitHub update assets directly to avoid release-metadata API rate limits
    * Support authenticated Windows delta updates when eligible, reconstruct and verify the complete target payload before requesting shutdown, and use the signed full package only when safe fallback rules permit
    * Display update release notes in a lightweight themed Markdown editor and migrate the last-update-check timestamp to machine-local session configuration
* **Interface and configuration**
    * Auto-hide view-split corner buttons until the tab strip is hovered, keeping tabs stationary, open menus usable and detached-window shortcuts active
    * Add Attach to Main Window beside the detached window's Stay on Top pin to move all tabs into the selected main split without losing unsaved edits or undo history; keep the pin color consistent with the active Open Windows button across theme changes
    * Show labels beside the Home and United Entry toolbar icons, matching Export
    * Restore the default Quick Note scheme for empty saved lists when upgrading from below `4.14.2`, including `4.14.1`; preserve custom schemes and later empty-list opt-outs
    * Add missing default United Entry aliases when upgrading from below `4.14.1` with a nonempty saved list, preserving custom definitions and explicit empty-list opt-outs
    * Refresh the English and Chinese welcome guides, README screenshots and development documentation
* **Reliability**
    * Fix CI recovery-archive, folded math-preview geometry, heading-navigation and production-view linkage fixtures; disable the repeatedly timing-out Linux view-area suite in CTest while retaining its build and Windows coverage
* **Translations**: Simplified Chinese (zh_CN), Japanese (ja) and Korean (ko) catalogs updated
