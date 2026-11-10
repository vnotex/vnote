## v4.14.3
A refinement release with smoother tree navigation, clearer editing controls and faster Quick Notes on top of VNote 4.14.2:

* **Navigation and tasks**
    * Add `Ctrl+G, F` for the View Window Mind Map action, configurable through `editor.core.shortcuts.MindMap`; existing profiles inherit it unless explicitly customized or disabled
    * Create a Quick Note directly when only one scheme is available, without opening the scheme picker
    * Expand or collapse task containers such as Git on double-click or Enter, including when filtering hides their children; keep leaf-task execution unchanged
* **Interface**
    * Animate expanding and collapsing Tasks, search results, Settings and United Entry trees consistently with Notebooks, Tags and Outline
    * Make tree views in every bundled theme except Native more compact with smaller indentation and horizontal spacing while preserving row height; keep inline renaming within the row and restore each tree's original spacing when returning to Native
    * Align notebook, search and task tree content with standard tree text by including theme borders and native text margins; preserve row heights and icon/count gaps
* **Editing and Markdown**
    * Update vtextedit to keep concealed-marker cursor boundaries consistent across keyboard and Vi navigation, and skip unnecessary cursor processing when persistent concealment is disabled
    * Rename the concealment control to Markers Concealment and clarify the None, Passive and Aggressive settings descriptions
    * Move the readable-width toolbar hint into shared Editor settings instead of Mind Map Editor settings
* **Translations**: Simplified Chinese (zh_CN) and Japanese (ja) catalogs updated
