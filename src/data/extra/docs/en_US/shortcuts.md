# Shortcuts
1. Shortcut letters such as `Ctrl+G` name keys; do not hold `Shift` unless it is shown. Reader navigation keys are case sensitive.
2. On macOS, `Ctrl` generally corresponds to `Command`, except in Vi mode and for the reader shortcuts noted below. Standard text-selection shortcuts below use Windows/Linux key names; macOS uses its platform text-navigation bindings.
3. The key sequence `Ctrl+G, I` means first press `Ctrl` and `G` together, release them, then press and release `I`.
4. These are **common default shortcuts**. To inspect or customize configurable bindings, open **Settings → Edit JSON** and edit `vnotex.json`: general shortcuts are in `core.shortcuts`, editor actions in `editor.core.shortcuts`. Save and restart VNote to use edited bindings. Built-in reader navigation and text-input bindings are not all listed in this file.
5. **Settings → General → Allow Ctrl+Alt shortcuts** is enabled by default. Turning it off disables configured shortcuts containing `Ctrl+Alt`, including global hotkeys, after a restart. This can avoid conflicts with AltGr; it does not erase your configured bindings.

## General
- `F11`  
Toggle full screen for the main window.
- `F10`  
Toggle keeping the main window on top.
- `Ctrl+G, E`  
Toggle expanding the content area.
- `Ctrl+Alt+P`  
Open Settings.
- `Ctrl+Alt+N`  
Create a note in the current folder.
- `Ctrl+Alt+S`  
Create a folder in the current folder.
- `Ctrl+Alt+Q`  
Start Quick Note capture (global hotkey).
- `Ctrl+Alt+U`  
Show the main window (global hotkey).
- `Ctrl+Alt+I`  
Open the first Quick Access item.
- `Ctrl+G, G`  
Open United Entry.
- `Ctrl+G, T`  
Open Export.
- `Ctrl+Shift+T`  
Reopen the last closed file.
- `Ctrl+G, X`  
Hide the focused dock; otherwise, close the current tab.
- `Ctrl+G, D`  
Locate the current note in the navigation panel.
- `Ctrl+J`/`Ctrl+K`  
Move down/up in widgets that support Vi-style navigation. These widget-navigation keys use physical **Control** on macOS, unlike the VSCode editor bindings below.

Global hotkeys require VNote to be running and the operating system to accept their registration.
The Search panel has no default activation shortcut. Open it from the sidebar, or configure `core.shortcuts.SearchDock` in `vnotex.json`.

### Panels and Focus
- `Ctrl+G, A`  
Activate the Notebooks dock.
- `Ctrl+G, U`  
Activate the Outline dock.
- `Ctrl+G, S`  
Activate the Snippets dock.
- `Ctrl+G, C`  
Activate the Location List dock.
- `Ctrl+G, Y`  
Focus the current note's content.

### Tabs and Splits
Focus a note or its tab pane to use the pane-scoped shortcuts.

- `Ctrl+G, 1` through `Ctrl+G, 9`  
Activate the corresponding tab in the current split.
- `Ctrl+G, 0`  
Switch to the previously active tab in the current split.
- `Ctrl+G, N`/`Ctrl+G, P`  
Activate the next/previous tab.
- `Ctrl+G, \`  
Create a split to the right.
- `Ctrl+G, -`  
Create a split below.
- `Ctrl+G, Shift+\`  
Maximize the current split.
- `Ctrl+G, =`  
Distribute splits evenly.
- `Ctrl+G, R`  
Remove the current split and its workspace.
- `Ctrl+G, H`/`Ctrl+G, J`/`Ctrl+G, K`/`Ctrl+G, L`  
Focus the split to the left/below/above/right.
- `Ctrl+G, Shift+H`/`Ctrl+G, Shift+J`/`Ctrl+G, Shift+K`/`Ctrl+G, Shift+L`  
Move the current tab to the split to the left/below/above/right.
- `Ctrl+G, Shift+D`  
Detach the current tab into a separate window.

## Note Views
These commands are view-scoped, not specific to the VSCode input mode. Availability depends on the current document type.

- `Ctrl+F`  
Open Find And Replace for the current note (PDF supports finding, not replacement).
- `F3`/`Shift+F3`  
Find the next/previous match using the last search.
- `Ctrl+G, Space`  
Clear search highlights in Markdown and text notes, including Markdown read mode.
- `Ctrl+G, O`  
Open the Outline toolbar popup in Markdown or PDF. This is separate from the Outline dock shortcut `Ctrl+G, U`.
- `Ctrl+=`/`Ctrl+-`  
Zoom in/out.
- `Ctrl+0`  
Reset zoom.
- `F9`  
Toggle presentation mode in Markdown, text, or PDF. Markdown switches to read mode and presents slides.
- `Escape`  
Leave presentation mode; if a popup is open, close the popup first.

## Text Editor
Save and zoom are shared editor commands. The text-input bindings below describe the default **VSCode** input mode. Select the input mode in **Settings → Text Editor → Input mode**; Normal and Vi modes have different bindings.

- `Ctrl+S`  
Save current changes.
- `Ctrl+Wheel`  
Zoom in/out using the mouse wheel.
- `Ctrl+J`/`Ctrl+K`  
Scroll down/up without moving the cursor.
- `Ctrl+Space` or `Ctrl+N`/`Ctrl+P`  
Activate word completion.
    - `Ctrl+N`/`Ctrl+P` or `Down`/`Up`  
    Browse the completion list.
    - `Enter`  
    Insert the selected completion.
    - `Escape`  
    Close the completion list.

### Text Editing
- `Shift+Left`, `Shift+Right`, `Shift+Up`, `Shift+Down`  
Extend the selection one character left or right, or one line up or down.
- `Ctrl+Shift+Left`, `Ctrl+Shift+Right`  
Extend the selection to a word boundary.
- `Ctrl+Shift+Up`, `Ctrl+Shift+Down`  
Extend the selection to the beginning or end of a paragraph.
- `Shift+Home`, `Shift+End`  
Extend the selection to the beginning or end of the current line.
- `Ctrl+Shift+Home`, `Ctrl+Shift+End`  
Extend the selection to the beginning or end of the note.
- `Ctrl+Shift+G`  
Go to line.
- `Ctrl+C`/`Ctrl+X`  
Copy/Cut the current line if there is no selection.
- `Ctrl+L`  
Select the current line.
- `Alt+Up`/`Alt+Down`  
Move the current line up/down.
- `Shift+Alt+Up`/`Shift+Alt+Down`  
Copy the current line up/down.
- `Ctrl+Shift+K`  
Delete the current line.
- `Ctrl+Shift+[`/`Ctrl+Shift+]`  
Fold/unfold the range at the cursor when folding is available.

## Markdown Editor
### Read Mode
Click the rendered content to use reader navigation. These navigation keys are inactive during presentation mode.

Use lowercase letters for the navigation keys below; `G` means `Shift+G`. The half-screen shortcuts (`Ctrl+U`, `Ctrl+D`) use the physical **Control** key even on macOS. For keyboard zoom and reset (`Ctrl+=`, `Ctrl+-`, `Ctrl+0`), use **Command** on macOS.

- `h`/`j`/`k`/`l`  
Scroll left/down/up/right.
- `Ctrl+U`  
Scroll up half a screen.
- `Ctrl+D`  
Scroll down half a screen.
- `gg`/`G`  
Jump to the beginning or end of the note (case sensitive).
- `Ctrl+Wheel`  
Zoom in/out using the mouse wheel (`Command+Wheel` on macOS).
- `Ctrl+Left Mouse Drag`  
Scroll in all directions (`Command+Left Mouse Drag` on macOS).
- Jump between headings (`<N>` is an optional count; the default is 1)
    - `<N>[[`: jump backward by `N` headings
    - `<N>]]`: jump forward by `N` headings
    - `<N>[]`: jump backward by `N` headings at the same level
    - `<N>][`: jump forward by `N` headings at the same level
    - `<N>[{`: jump backward by `N` headings one level higher
    - `<N>]}`: jump forward by `N` headings one level higher

Same-level and higher-level jumps stop at shallower section boundaries. A backward jump can first return to the current section heading if it has scrolled out of view.

### Markdown Presentation
Use `F9` to enter/leave presentation or `Escape` to leave. While presenting:

- `Right`/`Space`  
Go to the next slide.
- `Left`/`Shift+Space`  
Go to the previous slide.
- `Up`/`Down`  
Scroll within an oversized slide.
- `PageUp`/`PageDown`  
Scroll most of a page within the current slide.
- `Home`/`End`  
Scroll to the beginning/end of the current slide.

### Edit Mode
Shares the Text Editor shortcuts for the selected input mode.

- `Ctrl+T`  
Edit the current note, or save changes and exit edit mode.
- `Ctrl+G, Q`  
Discard current changes and exit edit mode.
- `Ctrl+G, V`  
Toggle Live Preview beside the Markdown editor.
- `Ctrl+G, B`  
Open the Tags popup for the note.
- `Ctrl+G, I`  
Open the snippet selector and insert a snippet (also available in the text editor).
- `Ctrl+Shift+V`  
Use alternate paste: plain text when rich paste is the default, or rich paste when plain text is the default.
- `Ctrl+G, Ctrl+P`  
Convert HTML from the clipboard to Markdown and paste it.

#### Text Editing
With a multi-line selection, inline formatting applies to each line's content while preserving list, quote, and heading prefixes.

- `Ctrl+B`  
Format selected text as bold, or insert bold markers at the cursor.
- `Ctrl+I`  
Format selected text as italic, or insert italic markers at the cursor.
- `Ctrl+;`  
Wrap selected text as inline code, or insert inline-code markers at the cursor.
- `Ctrl+'`  
Insert a fenced code block or wrap selected lines in one. Repeating the action inside an empty block removes its fences; it does not generally exit a nonempty block.
- `Ctrl+.`  
Wrap selected text as inline math, or insert inline-math markers at the cursor.
- `Ctrl+G, .`  
Insert a math block or wrap selected lines in one. Repeating the action inside an empty block removes its delimiters.
- `Ctrl+G, M`  
Mark selected text with HTML `<mark>` tags, or insert the tags at the cursor.
- `Ctrl+,`  
Insert or edit a link.
- `Ctrl+/`  
Insert a table.
- `Ctrl+<Num>`  
Insert a heading at level `<Num>`, where `<Num>` is 1 to 6. Applies to the current line or selected lines.
- `Ctrl+7`  
Remove heading markers from the current line or selected lines.
- `Ctrl+8`/`Ctrl+9`  
Insert unordered/ordered list markers on the current line or selected lines.
- `Tab`/`Shift+Tab`  
Increase/decrease indentation in Markdown source; a selection applies this to the selected lines. In an editable table-preview cell, move to the next/previous cell instead.
- `Shift+Enter`  
Insert two spaces followed by a newline: a Markdown hard line break.

## Navigation Mode
`Ctrl+G, W` enters **Navigation Mode**. VNote displays one or two letters on major widgets and navigation targets. Press the displayed letters to jump to the corresponding target.
