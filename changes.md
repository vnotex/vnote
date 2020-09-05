# Changes History
## v2.10
- Add support for Git push/pull; (@musmus9405)
- Bug fixes; (@tootal, @hlyang1992)

## v2.9.1
- Fix start issue on macOS (no need to add `-m` option);

## v2.9
- Upgrade to MathJax v3;
- Support `e` command in Vim mode; (@gydeng)
- Fix start issue on macOS (no need to add `-m` option);

## v2.8.2
- Upgrade Mermaid.js;

## v2.8.1
- Fix PlantUML online resources;
- MacOS: support staying in tray;
- Add a configuration in Settings to control code block copy button;

## v2.8
- Add supports for image hosting, including service GitHub, Gitee, Wechat, and Tencent Cloud; (@fengwenhua)
- Support sorting notebooks in context menu;
- Add theme v_next; (@hzhu212);

## v2.7.2
- Use 5.9.8 on macOS;
- Call Java (PlantUML) directly on Win;

## v2.7.1
- Fix PlantUML command;
- Use 5.12.0 on macOS;
- Add config [web]/enable_code_block_copy_button for copy button in code block in read mode;

## v2.7
- Editor
    - Fix smart table bugs;
    - Fix improper leading line space on macOS;
- Fix bug which will open two tabs for the same file;
- Add copy button to copy code block in read mode;
- PluatUML: use shell to launch Java;
- Use qt 5.12.1 on macOS for better performance;
- Add theme `v_simple` provided by user LuoshuiTianyi;
- Support adding sibling folder in context menu;

## v2.6
- Version-specific user track;

## v2.5
- Do not open unknown type file when creating a new note;
- Support Right-To-Left text in edit mode (with Vim mode disabled);
- Remove credict information in export template;

## v2.4
- Bundle qt5ct in Linux for native look support;
- Use `Return` or `Enter` to confirm selection in macOS (besides `Cmd+O`);
- Support sync note list to current note automatically via config `global/sync_note_list_to_current_tab`;
- Add `Ctrl+E N` to view and edit current note's information;
- Fix Markdown highlighter to handle fenced code block without a leading empty line;
- Prompt for restart after changing theme;

## v2.3
- Add Japanese translations (thanks to @miurahr);
- Editor
    - Bug fix of smart table;
    - Allow to disable smart table;
- Update to Qt 5.9.7 in CI in Linux;
- Add user track logics for users counting;
- Add CMake build system;
    - Support multiple packaging genration;
      - QtIFW GUI installer
      - Debian package
      - RPM package
      - AppImage
      - Tar.gz package
      - ZIP package
      - Windows Nullsoft installer
      - Windows NuGet package
      - Mac OS X dmg package
    - Support staic code checkers;
      - clang-tidy
      - crazy
      - include-what-you-see
    - Support modern IDEs;
      - Microsoft Visual Studio 2017
      - JetBrains CLion
      - QtCreator
      - XCode
      - VSCode

## v2.2
- Editor
    - Support smart GFM table;
    - support auto scrolling cursor into center;
    - Support specifying font via settings to override style config;
    - Guess image title from the file name;
    - Support ~~~ as fenced code block marker;
- MathJax
    - Support equation number;
    - Support \begin and \end directly;
    - Support $..$ in \text{} within $$...$$;
- NotebookPanel: support spliting file list out;
- Add openGL option in settings on Windows;
- Markdown-it
    - Support alert text via markdown-it-container plugin;
- Support WaveDrom for digital timing diagram;

## v2.1
- UniversalEntry/Searcher: `Ctrl+B` to expand/collapse all items;
- Captain mode: support specifying keyboard layout mappings, such as Russian or French;
- Editor
    - Support downloading images to local when Parse&Paste;
    - Support scaling image when inserting image;
    - Support Parse&Paste as table from Microsoft Excel;
    - Support attach a file as attachment and insert as link by Drag&Drop;
    - Support `Ctrl+Sfhit+V` to paste as plain text;
    - Fix Unicode issue;
- Attachment
    - Support copying file path;
- Support Drag&Drop to copy/move notes;
- Support MathJax euqation number in read mode;
- Fix blurry toolbar icon on macOS;
- Fix blurry renderring in read mode on Windows;

## v2.0
- Vim: support specifying leader key via "[editor]/vim_leader_key";
- LivePreview
    - Smart live preview for PlantUML (Live Preview Tunnel);
    - Expand live preview area via `Ctrl+E U`;
- In-Place Preview: support in-place preview for online PlantUML;
- Captain: fix captain mode shortcuts in different keyboard layout (Windows only);
- Quick Access: specify a note for quick access via `Ctrl+Alt+I`;
- Magic Word: support `%att%` as the relative path of the attachment folder;
- Search: highlight matches of full-text search results in page in edit mode;
- Editor
    - Allow inserting link or content when dropping files in;
    - Support highlighting tabs;
- Support parsing HTML tables without head;
- Export: support outline panel in exported HTML files;

## v1.22
- Editor
    - Support parsting HTML as converted Markdown text;
    - Improve performance of in-place preview;
    - Be aware of links, images, and in-place preview in edit mode (visit or copy);
    - Support exporting and copying PlantUML and Graphviz diagram in edit mode;
- Support middle click to close a tab;
- Support searching files via multiple tags, such as "vnote && markdown";

## v1.21
- Improve performance of editor (now could process 10K lines);
- Support `<mark>vnote</mark>` highlight in edit mode;
- Support **completion** in edit mode by `Ctrl+N/P`;
- Support building a notebook from external directory recursively;
- Update Flowchart.js to 1.11.3;

## v1.20
- More efficient and accurate Markdown parser and highlighter;
- MathJax: get rid of escaping;
- Draw background for **hrule** in edit mode;
- Support specifying the expanded level of outline;
- Support drag&drop a directory to Explorer to open it;
- `Ctrl+;` to insert inline code and `Ctrl+J/K` to scroll page in edit mode;
- Support YAML front matter in edit mode;
- Support searching Explorer root directory;

## v1.19
- Support tags for notes;
- UniversalEntry: support searching tags;
- WebView: click an image or diagram to view it in read mode;
- Add test buttons for PlantUML and Graphviz in settings dialog;
- CaptainMode: Y to focus to the edit area;

## v1.18
- Explorer: fix the crash when no root entry is set;
- New dark theme **v_detorte** from a Vim *detorte* theme;
- Support maximizing split or distributing splits;
- Refine styles and HiDPI support;

## v1.17
- Add **History** to browse history;
- Add **Explorer** to browse external files;
- Support view order in note list;
- Support relative path for notebook;
- UniversalEntry
    - Fix input method issue on macOS;
    - Add `j` for listing and searching History;
- Support customized zoom delta of editor;
- Add cache for in-place preview;
- Better support for hiDPI;
- Lazy initialization;
- Support stay-on-top;

## v1.16
- Markdown-it: supports specifying image size, emoji, and YAML metadata;
- Bug fixes;

## v1.15
- Support **PlantUML** and **Graphviz**;
- **In-Place Preview** for MathJax, PlantUML, Graphviz, and Flowchart.js;
- **Live Preview** for diagrams via `Ctrl+E I`;
- Restore cursor position when recovering pages at startup;
- UniversalEntry
    - Ctrl+I to expand/collapse current item;
    - Ctrl+L to go to current item's parent item;
- Markdown-it: aware of YAML format metadata in notes;
- Show hovered link in status line in read mode;
- Export: support embedding images as data URI into HTML pages;

## v1.14
- Support **Universal Entry** by `Ctrl+G`;
- Single click a note in note list to open it in a new tab by default;
- Translate `Ctrl` in default shortcuts to `Meta` on macOS;
- Do not copy files when import if they locate in current folder;

## v1.13
- Replace **v_white** theme with **v_native**, which behaves more like native applications;
- Support **full-text search**;
    - Support `&&` and `||` logics (space-separated keywords are treated as AND);
- Enhanced export;
    - Support MHTML format;
    - Support All-In-One PDF via tool *wkhtmltopdf*;
    - Support *pandoc* and random tool for custom export;
- Support **Word Count** information in both read and edit mode;
- Support SavePage action in read mode;
- Support *back reference* in replace text via `\1`, `\2`, and so on;
- Support sorting in Cart;
- Support sorting notes and folders via name or modification date;
- Support both `flow` and `flowchart` as the language of *flowchart.js* diagram;
- Add PasteAsBlockQuote menu action to paste text as block quote from clipboard;
- Add options for Markdown-it to support subscript and superscript;
- Better support for 4K display;

## v1.12
- Combine `EditNote` and `SaveExitNote` as `EditReadNote` (`Ctrl+T`);
- Support exporting notes as Markdown, HTML, and PDF;
- Support simple search in directory tree, file list, and outline;
- Support copying selected text as HTML in edit mode;
- Support copying text to Evernote, OneNote, Word, WeChat Public Account editor and so on;
- Support auto-save;
- Support fullscreen mode and hiding menu bar;
- Support `Ctrl+H/W/U` to delete text in most line edits;
- Support zooming in/out in edit mode;
- Support MathJax in fenced code block with language `mathjax` specified;
- More shortcuts;
- Add **Cart** to collect notes for further processing;
- Output built-in themes on start of VNote;
- `Esc` to exit edit mode when Vim mode is disabled;
- Support Vim command line for search in read mode;
- Support printing;
- Single click in file list to open file in current tab, double clicks to open in a new tab;

## v1.11.1
- Refine copy function in read mode. Better support for copying and pasting into OneNote or WeChat editor;
- Do not highlight code blocks without language specified by default;
- Refine themes and styles;
- Support foreground for selected/searched word in MDHL style;
- Support shortcuts for external programs;
- Support resetting VNote;
- Cover more scenarios for Chinese translations;

## v1.11
- Support themes;
    - Three built-in mordern themes;
    - One dark mode theme;
- Vim mode
    - Support block cursor in Normal/Visual mode;
    - `=` to auto-indent selected lines as the first line;
- Support custom external editors to open notes;
- Enable `Ctrl+C`/`Ctrl+V` in Vim mode to copy/paste;
- Support Flash Page to record ideas quickly;
- Support previewing inline images;

## v1.10
- Migrate to Qt 5.9.1;
- Support Compact mode in main window;
- Update icons;
- Support custom startup pages;
- Remove obsolete title marker when inserting new one;
- Support Magic Words;
- Vim mode
    - Share registers among all tabs;
    - Support `Ctrl+O` in Insert mode;
- Add "Code Block", "Insert Link", and "Insert Image" tool bar buttons;
- Support `Ctrl+Shift+T` to recover last closed tabs;
- Support view read-only files in edit mode;
- Refactor editor for speed;
- Support templates when creating notes;
- Support snippets;
- Support file change check;
- Support backup file (save changes automatically);

## v1.9
- Support attachments of notes.
- Add recycle bin to notebook to hold deleted files.
- Refine Vim mode:
  - Support J and gJ to join line;
  - Support S, {, and };
  - <leader>w to save note;
  - Fix Y and D actions in Visual mode.
- Support AppImage package for Linux.
- More responsive and efficient syntax highlight and image preview.
- More pleasant line distance.
- More natural interaction of folder and note management.
- Support inserting note name as title.
- Support custom default mode to open a note.
- Support auto heading sequence.
- Support color column in fenced code block in edit mode.
- Support line number in code block in both read and edit mode.
- Support created time and modified time of notes, folders, and notebooks.
- Support custom Markdown-it options, such as auto line break.
- Confirm when cleaning up unused images.
- Support custom Mathjax location.
- Support custom style for code block highlights in read mode.
- Double click on a tab to close it.

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
