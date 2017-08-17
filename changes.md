# Changes History
## v1.8
- Support editing external files. VNote could open files from command line.
- Support drag-and-drop to open external files.
- Refine tab context menu.
- Support system tray icon.
- Refine Vim mode.
- Make all tool buttons always visible and smaller.
- Support custom file type suffix.
- Make the name of notebook/folder/note case-insensitive.
- Support links to internal notes in notes.

## v1.7
- ATTENTION: please add font-size option to the "editor" section of your custom MDHL style.
- Refine Vim mode (more functions, please refer to the shortcuts help).
- Support Find in Vim mode.
- Refine tab context menu.
- Support Flowchart.js for flowchart.
- Add toolbar for common text edit functions.
- Support line number (both absolute and relative) in edit mode.
- Support custom shortcuts.
- Support [[, ]], [], ][, [{, ]} to navigate through titles in both edit and read mode.
- Many minor bug fixes.

## v1.6
- Support simple but powerful **Vim mode**.
- Change the shortcut of ExitAndRead from `Ctrl+R` to `Ctrl+T`.
- Add a edit status indicator in the status bar.
- Dragging mouse with Ctrl and left button pressed to scroll in read and edit mode.
- Refine highlighting cursor line.
- Support subscript, superscript and footnote in markdown-it renderer.
- Refactor outline logics to not show extra [EMPTY] headers.
- Handle HTML comments correctly.
- Provide a default root folder when adding notebooks.
- Support check for updates.
- Redraw app icons.
- Many minor bug fixes.

## v1.5
- Support logging in release mode.
- Fix Chinese font matching in mdhl.
- Fix VimagePreviewer to support optional title.
- Refactor local image folder logics.
- Support custom local image folder for both notebook scope and global scope.
- Support constraining the width of images in read mode.
- Fix and refine default style.
- Centering the images and display the alt text as caption in read mode.
- Support exporting a single note as PDF file.
- Add "Open File Location" menu item in folder tree and note list.
- Support highlighting trailing space.

## v1.4
- Use `_vnote.json` as the config file.
- More user friendly messages.
- Delete notebook by deleting root directories one by one.
- Refactor image preview logics to support previewing all images in edit mode.
- Support constraining the width of previewed images to the edit window.
- bugfix.

## v1.3
- Support code block syntax highlight in edit mode.
- A more pleasant AutoIndent and AutoList.
- `Ctrl+<Num>` instead of `Ctrl+Alt+<Num>` to insert title.
- Support custom Markdown CSS styles and editor styles.

## v1.2
- Support **MathJax**.
- Fix a crash on macOS.
- Change default font family.
- Refine tab order.
- Better support for HiDPI.
- Support zoom in/out page when reading.
- Introduce **Captain Mode** and **Navigation Mode**.
- A more user friendly popup opened notes list.
- Support jumping to specified tab efficiently by num keys.
- Add shortcuts documentation.
- AutoList and AutoIndent.

## v1.1
- Refine messages and dialogs. Add Chinese translations.
- A new application icon.
- Support install target for Linux.
- Continuous build and deployment for Linux, macOS, and Windows.
- Support both X64 and x86 version of Windows.
- Add `.md` suffix automatically when creating a note.
- A more user friendly insert dialog.
- Support **Mermaid** diagram.
- Add **markdown-it** as the default renderer. Support task list.
