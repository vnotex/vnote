## v4.15.2
A maintenance release with steadier TODO planning, improved Markdown editing and clearer file navigation on top of VNote 4.15.1:

* **TODO planner**
    * Keep My Day actions and task-row labels correct across midnight, including expired My Day membership
    * Toggle task details by clicking the selected task again
    * Fix sidebar group drag-and-drop reordering and keep groups before ungrouped lists
    * Remember the task sort order across TODO files and restarts, with Manual available to restore document order
    * Disable document zoom in the TODO editor to keep its task layout stable
* **Markdown and images**
    * Offer Open Link and Copy Link for bare URLs in Markdown edit mode, using parsed link destinations and excluding code
    * Prevent cursor jumps at folded document tails
    * Stop the mouse release that opens the Insert Table popup from also triggering its dialog
    * Default clipboard image files to lossless PNG for new profiles and missing or invalid format settings; profiles already storing JPEG keep JPEG, now encoded at quality 100 ([#2791](https://github.com/vnotex/vnote/issues/2791))
* **Navigation and interface**
    * Add built-in file-type icons to node explorers using upstream Tabler artwork
    * Restore tree branch connector lines in the Pure theme, retaining its expand/collapse arrows
* **Translations**: Simplified Chinese (zh_CN) and Japanese (ja) catalogs updated
