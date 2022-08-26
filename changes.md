# Changes
## v3.15.0
* Editor supports Word Count
* Add Open Windows panel
* Theme: add Vue-light theme
* Support default open mode
* NotebookSelector: support dynamic icons for notebooks

## v3.14.0
* Theme: support custom icons
* Theme: refine icons
* NavigationMode: fix issue for input method

## v3.13.1
* Shortcuts for Copy/Paste/Properties in node explorer
* Global shortcut to call out main window
* UnitedEntry: bug fix for macOS

## v3.13.0
* United Entry: migration of Universal Entry

## v3.12.888
* Fix shortcuts in key sequence with input method (like `Ctrl+G, E`)
* Add line ending settings for config files
* FindAndReplace: fix zero-length search
* QuickAccess: support folders
* Upgrade to Qt 5.15.2
* Support file associations
* NewNoteDialog: remember default file type

## v3.12.0
* NotebookExplorer: support separate node explorer
* Theme: add user-provided VSCode-Dark theme
* MarkdownEditor: use web to highlight code blocks
* MarkdownViewWindow
    * Add switch for code block line number
    * Fix ParseToMarkdown `<style>` issue
    * Add config for overridding MathJax script
* SortDialog: fix sorting issue of date
* FramelessMainWindow: fix StayOnTop issue

## v3.11.0
* Task: support a simple task system (@tootal)
* Theme: add user-provided Solarized-Dark and Solarized-Light themes
* Export: fix wkhtmltopdf table-of-contents translation
* Support equation begin in MathJax
* MainWindow: decide DPI on the screen vnote starts
* Settings: support searching
* Fix crash caused by Youdao Dict

## v3.10.1
* MarkdownEditor: fix view mode issue
* Support print
* Refine icons

## v3.10.0
* MarkdownEditor
    * Support side-by-side edit with preview
    * Support config for highlighting whitespace
* Tag: fix input method issue on macOS

## v3.9.0
* Remove recycle bin node (now recycle bin is just a simple folder)
* Quick Access: support removing items directly
* MarkdownEditor
    * Support centering images in read mode
    * Add user.css for user styles in read mode
    * Add debugger by F12
    * Support context-sensitive context menu for images and links

## v3.8.0
* Support tags
* Introduce notebook database using SQLITE
* A perfect frameless main window on Windows
* Add switch to control whether store history in notebook
* Refine dock widgets of main window
* NotebookExplorer: support scan notebook and import external files

## v3.7.0
* PlantUml/Graphviz: support relative path executable
* macOS: support opening file with VNote in Finder
* Sort notes by name case-insensitively
* Export
    * Support All-in-One in PDF format
    * Support Custom export format (like Pandoc)
    * Allow minimizing the export dialog and doing export at background
* MainWindow: use icon-only bar for docks
* Support update check
* Add shortcuts for CloseOtherTabs and CloseTabsToTheRight
* Search: highlight matched items in opened files
* Editor: support specifying line ending

## v3.6.0
* Support **Image Host**: GitHub and Gitee
* Add config page for Vi

## v3.5.1
* LocationList: fix recently introduced regression when highlighting segments of text

## v3.5.0
* Support History
* ViewArea
    * `Ctrl+G, H/J/K/L` to navigate through ViewSplits
    * `Ctrl+G, Shift+H/J/K/L` to move ViewWindow across ViewSplits
* MarkdownEditor
    * Add configs for in-place preview sources
    * Add a tool button to disable in-place preview
* Vi: support align and indent commands `=` and `>`
* LocationList: highlight matched text segments
* SelectDialog: support shortcuts (such as Rich Paste)

## v3.4.0
* Support Snippet
    * `Ctrl+G S` to insert a snippet
    * `%snippet_name%` to insert a snippet (the legacy Magic Word)
        * Snippet is supported in some dialogs (such as creating a new note)
* Support note template (snippet is supported)
* Remove `'` and `"` from auot-brackets

## v3.3.0
* Editor: support auto indent, auto list
* Support opening notes with external programs
* Add a delay after code/math blocks update before preview

## v3.2.0
* Support local PlantUml and Graphviz rendering
* Add shortcuts to tab navigation in ViewSplit
* Editor: support auto bracket and overridding font family

## v3.1.0
* Support Japanese translation (Thanks @miurahr)
* MarkdownEditor: guess image suffix when fetching to local
* Refine read mode styles (Thanks @heartnn)
* Support recovering edit session on start
* Support recovering notebook explorer session on start
* Support Flash Page
* Support Quick Access
* Allow to keep docks (like Outline) when expanding content area

## v3.0.1
* Support spell check via Hunspell
* `Ctrl+Alt+F` to trigger full-text search
* Auto focus to the input widget when activating full-text search
* Fix Expand Content Area with panels

## v3.0.0-beta.11
* Full-text search

## v3.0.0-beta.10
* Show more tips
* Add exclude patterns for external nodes
* Add command line parser and support opening files from command line
* Update Mermaid.js to 8.9.1
* Support exporting current note
* Add ExpandAll to node explorer

## v3.0.0-beta.9
* Fix crash when exporting external files
* Support manual sorting folders and notes
* Support showing external files in notebook

## v3.0.0-beta.8
* Note explorer supports different view orders
* Fix `Ctrl+V` paste in editor
* Fix Linux HTTPS crash bug
* Add button to show/hide recycle bin node
* Other small fixes

## v3.0.0-beta.7
* Support export to Markdown/HTML/PDF
* Support base level 1/2/3 for section number in read mode
* Support opening link to folder in read mode

## v3.0.0-beta.6
* Add theme **pure** for light mode
* Small fixes

## v3.0.0-beta.5
* Refine themes
* Add section number style "1.1" and "1.1.", and use the later as default
* Support indentation of first line of paragraph in read mode
* Add file type combo box in NewNoteDialog
* Add "Insert Mark" in tool bar
* Support **Smart Table**
* Support `*.rmd` as Markdown suffix
* Turn on system title bar by default
* Enable AutoBreak by default

## v3.0.0-beta.4
* Add theme **moonlight** for dark mode
* Add shortcuts for split and workspace
* Bug fix
* More configs in Settings dialog for Markdown viewer
* Support section number in edit mode (without the dot suffix)
* Support link jump in Markdown viewer
* Use socket for single instance guard

## v3.0.0-beta.3
* Support `[TOC]`
* Fix MathJax
* Add shortcut for StayOnTop
* Add Markdown guide and shortcuts guide
* Show hovered link in read mode
* Smart Input Method in Vi mode
* Bug fix in Markdown editor when finding current heading index
* Add custom Info.plist on macOS
* Support minimizing to system tray
* Support restart
* Add read and edit in Markdown ViewWindow
* Add Chinese translations

## v3.0.0-beta.2
* Fix Import Legacy Notebook
* Refine toolbar
* Fix Outline viewer
