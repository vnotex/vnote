# VNote Shortcuts
1. All the keys without special notice are **case insensitive**;
2. On macOS, `Ctrl` corresponds to `Command`;

## Normal Shortcuts
- `Ctrl+T`  
Toggle expanding the edit area.
- `Ctrl+N`  
Create a note in current directory.
- `Ctrl+F`  
Find/Replace in current note.
- `Ctrl+Q`  
Quit VNote.
- `Ctrl+J`/`Ctrl+K`  
VNote supports `Ctrl+J` and `Ctrl+K` for navigation in the notebooks list, directories list, notes list, opened notes list, and outline list.

### Read Mode
- `Ctrl+W`  
Edit current note.
- `H`/`J`/`K`/`L`  
Navigation, corresponding to Left/Down/Up/Right arrow keys.
- `Ctrl+U`  
Scroll up half screen.
- `Ctrl+D`  
Scroll down half screen.
- `gg`/`G`  
Jump to the beginning or end of the note. (Case Sensitive).
- `Ctrl + +/-`    
Zoom in/out the page.
- `Ctrl+Wheel`    
Zoom in/out the page through the mouse scroll.
- `Ctrl+0`  
Recover the page zoom factor to 100%.

### Edit Mode
- `Ctrl+S`  
Save current changes.
- `Ctrl+R`  
Save current changes and exit edit mode.

#### Text Editing
- `Ctrl+B`  
Insert bold. Press `Ctrl+B` again to exit. Currently selected text will be changed to bold if exist.
- `Ctrl+I`  
Insert italic. Press `Ctrl+I` again to exit. Currently selected text will be changed to italic if exist.
- `Ctrl+O`  
Insert inline code. Press `Ctrl+O` again to exit. Currently selected text will be changed to inline code if exist.
- `Ctrl+H`  
Backspace. Delete a character backward.
- `Ctrl+W`  
Delete all the characters from current cursor to the first space backward.
- `Ctrl+U`  
Delete all the characters from current cursor to the beginning of current line.
- `Ctrl+Alt+<Num>`  
Insert title at level `<Num>`. `<Num>` should be 1 to 6. Currently selected text will be changed to title if exist.
- `Tab`/`Shift+Tab`  
Increase or decrease the indentation. If any text is selected, the indentation will operate on all these selected lines.
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

# Captain Mode
To efficiently utilize the shortcuts, VNote supports the **Captain Mode**.

Press the leader key `Ctrl+E`, then VNote will enter the Captain Mode, within which VNote supports more efficient shortcuts.

By the way, in this mode, `Ctrl+W` and `W` is equivalent, thus pressing `Ctrl+E+W` equals to `Ctrl+E W`.

- `E`   
Toggle expanding the edit area.
- `P`   
Toggle single panel or two panels mode.
- `T`   
Toggle the Tools panel.
- `F`   
Popup the opened notes list of current split window. Within this list, pressing the sequence number in front of each note could jump to that note.
- `X`   
Close current tab.
- `J`   
Jump to next tab.
- `K`   
Jump to last tab.
- `1` - `9`  
Number key 1 to 9 will jump to the tabs with corresponding sequence number.
- `0`   
Jump to previous tab. Alternate between current and previous tab.
- `D`   
Locate to the directory of current note.
- `Q`   
Discard current changes and exit edit mode.
- `V`   
Vertically split current window.
- `R`   
Remove current split window.
- `H`   
Jump to the first split window on the left.
- `L`   
Jump to the first split window on the right.
- `Shift+H`   
Move current tab one split window left.
- `Shift+L`  
Move current tab one split window right.
- `?`   
Display shortcuts documentation.

## Navigation Mode
Within the Captain MOde, `W` will turn VNote into **Navigation Mode**. In this mode, VNote will display at most two characters on some major widgets, and then pressing corresponding characters will jump to that widget.
