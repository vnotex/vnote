## v4.15.1
A refinement release with smoother TODO planning, unified editor settings and faster notebook searches on top of VNote 4.15.0:

* **TODO planner and dashboard**
    * Commit list names when editing finishes, preserving pending names when switching lists or saving
    * Expand and collapse TODO groups with a single click, and let task notes fill the available details pane
    * Add My Day icon marks beside importance stars in the TODO editor and dashboard sticker for direct membership changes
    * Remember Lists and Task details panel widths across list switches, narrow layouts and restarts, and fix cropped list emoji icons
* **Editing and interface**
    * Default note opening to Edit; upgrading from versions below `4.15.1` resets the previous opening mode to Edit, while later saved Read preferences remain respected
    * Centralize Normal, Vi and VSCode modes in shared Input Mode settings, preserving existing selections across text and Markdown editors
    * Keep empty concealed Markdown elements visible and use a sticky-note icon for mind-map node notes
    * Display Created and Modified timestamps in note and folder properties using the application locale, preserving local time and seconds
* **Search and reliability**
    * Use typed vxcore results for folder listings, search and tags to avoid in-process JSON round trips while preserving metadata, ordering and existing JSON APIs
    * Remove the ripgrep backend and use built-in search for consistent Unicode replacement and cooperative cancellation
    * Batch raw-notebook search metadata checks in the HTTP API ([#2789](https://github.com/vnotex/vnote/pull/2789))
    * Fix deleting notebook folders whose ordinary descendants have missing index metadata, while retaining protected-file and unsafe-path checks
* **Translations**: Simplified Chinese (zh_CN) and Japanese (ja) catalogs updated
