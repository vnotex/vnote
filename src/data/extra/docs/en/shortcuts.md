# Shortcuts
1. All the keys without special notice are **case insensitive**;
2. On macOS, `Ctrl` corresponds to `Command` except in Vi mode.

## General
- `Ctrl+G E`  
Toggle expanding the content area.
- `Ctrl+Alt+N`  
Create a note in current folder.
- `Ctrl+F`  
Find/Replace in current note.
- `Ctrl+Alt+F`  
Advanced find.
- `Ctrl+J`/`Ctrl+K`  
VNote supports `Ctrl+J` and `Ctrl+K` for navigation in many widgets.
- `Ctrl+Left Mouse`  
Scroll in all directions.
- `Ctrl+Shift+T`  
Recover last closed file.
- `Ctrl+Alt+L`  
Open Flash Page.
- `Ctrl+Alt+I`  
Open Quick Access.
- `Ctrl+G 1`  
Focus the Navigation dock.
- `Ctrl+G 2`  
Focus the Outline dock.
- `Ctrl+G X`   
Close current tab.
- `Ctrl+G D`   
Locate to the folder of current note.
- `Ctrl+G O`   
Open the Outline popup.

## Text Editor
- `Ctrl+S`  
Save current changes.
- `Ctrl+Wheel`  
Zoom in/out the page through the mouse scroll.
- `Ctrl+J/K`  
Scroll page down/up without changing cursor.
- `Ctrl+N/P`  
Activate auto-completion.
    - `Ctrl+N/P`  
    Navigate through the completion list and insert current completion.
    - `Ctrl+E`  
    Cancel completion.
    - `Enter`  
    Insert current completion.
    - `Ctrl+[` or `Escape`  
    Finish completion.

### Text Editing
- `Shift+Left`, `Shift+Right`, `Shift+Up`, `Shift+Down`  
Expand the selection one character left or right, or one line up or down.
- `Ctrl+Shift+Left`, `Ctrl+Shift+Right`  
Expand the selection to the beginning or end of current word.
- `Ctrl+Shift+Up`, `Ctrl+Sfhit+Down`  
Expand the selection to the beginning or end of current paragraph.
- `Shift+Home`, `Shift+End`  
Expand the selection to the beginning or end of current line.
- `Ctrl+Shift+Home`, `Ctrl+Shift+End`  
Expand the selection to the beginning or end of current note.

## Markdown Editor
### Read Mode
- `H`/`J`/`K`/`L`  
Navigation, corresponding to Left/Down/Up/Right arrow keys.
- `Ctrl+U`  
Scroll up half screen.
- `Ctrl+D`  
Scroll down half screen.
- `gg`/`G`  
Jump to the beginning or end of the note. (Case Sensitive).
- `Ctrl+=/-`  
Zoom in/out the page.
- `Ctrl+Wheel`  
Zoom in/out the page through the mouse scroll.
- `Ctrl+0`  
Recover the page zoom factor to 100%.
- Jump between titles
    - `<N>[[`: jump to previous `N` title;
    - `<N>]]`: jump to next `N` title;
    - `<N>[]`: jump to previous `N` title at the same level;
    - `<N>][`: jump to next `N` title at the same level;
    - `<N>[{`: jump to previous `N` title at a higher level;
    - `<N>]}`: jump to next `N` title at a higher level;

### Edit Mode
Shares the same shortcuts with Text Editor.

- `Ctrl+T`  
Edit current note or save changes and exit edit mode.
- `Ctrl+G Q`   
Discard current changes and exit edit mode.

#### Text Editing
- `Ctrl+B`  
Insert bold. Press `Ctrl+B` again to exit. Current selected text will be changed to bold if exists.
- `Ctrl+I`  
Insert italic. Press `Ctrl+I` again to exit. Current selected text will be changed to italic if exists.
- `Ctrl+;`  
Insert inline code. Press `Ctrl+;` again to exit. Current selected text will be changed to inline code if exists.
- `Ctrl+'`  
Insert fenced code block. Press `Ctrl+'` again to exit. Current selected text will be wrapped into a code block if exists.
- `Ctrl+,`  
Insert inline math. Press `Ctrl+,` again to exit. Current selected text will be changed to inline math if exists.
- `Ctrl+.`  
Insert math block. Press `Ctrl+.` again to exit. Current selected text will be changed to math block if exists.
- `Ctrl+/`  
Insert table.
- `Ctrl+<Num>`  
Insert title at level `<Num>`. `<Num>` should be 1 to 6. Current selected text will be changed to title if exists.
- `Ctrl+7`  
Delete the title mark of current line or selected text.
- `Tab`/`Shift+Tab`  
Increase or decrease the indentation. If any text is selected, the indentation will operate on all these selected lines.
- `Shift+Enter`  
Insert two spaces followed by a new line, namely a soft linebreak in Markdown.

## Navigation Mode
`Ctrl+G W` will turn VNote into **Navigation Mode**. In this mode, VNote will display at most two characters on some major widgets, and then pressing corresponding characters will jump to that widget.
