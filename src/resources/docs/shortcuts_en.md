# VNote Shortcuts
1. All the keys without special notice are **case insensitive**;
2. On macOS, `Ctrl` corresponds to `Command` except in Vim mode.

## Normal Shortcuts
- `Ctrl+E E`  
Toggle expanding the edit area.
- `Ctrl+Alt+N`  
Create a note in current folder.
- `Ctrl+F`  
Find/Replace in current note.
- `Ctrl+Q`  
Quit VNote.
- `Ctrl+J`/`Ctrl+K`  
VNote supports `Ctrl+J` and `Ctrl+K` for navigation in the notebooks list, directories list, notes list, opened notes list, and outline list.
- `Ctrl+Left Mouse`  
Scroll in all directions.

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
- Jump between titles
    - `[[`: jump to previous title;
    - `]]`: jump to next title;
    - `[]`: jump to previous title at the same level;
    - `][`: jump to next title at the same level;
    - `[{`: jump to previous title at a higher level;
    - `]}`: jump to next title at a higher level;

### Edit Mode
- `Ctrl+S`  
Save current changes.
- `Ctrl+T`  
Save current changes and exit edit mode.

#### Text Editing
- `Ctrl+B`  
Insert bold. Press `Ctrl+B` again to exit. Current selected text will be changed to bold if exist.
- `Ctrl+I`  
Insert italic. Press `Ctrl+I` again to exit. Current selected text will be changed to italic if exist.
- `Ctrl+D`  
Insert strikethrought. Press `Ctrl+D` again to exit. Current selected text will be changed to strikethrough if exist.
- `Ctrl+O`  
Insert inline code. Press `Ctrl+O` again to exit. Current selected text will be changed to inline code if exist.
- `Ctrl+H`  
Backspace. Delete a character backward.
- `Ctrl+W`  
Delete all the characters from current cursor to the first space backward.
- `Ctrl+U`  
Delete all the characters from current cursor to the beginning of current line.
- `Ctrl+<Num>`  
Insert title at level `<Num>`. `<Num>` should be 1 to 6. Current selected text will be changed to title if exist.
- `Tab`/`Shift+Tab`  
Increase or decrease the indentation. If any text is selected, the indentation will operate on all these selected lines.
- `Shift+Enter`  
Insert two spaces followed by a new line, namely a soft linebreak in Markdown.
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

## Custom Shortcuts
VNote supports customing some standard shortcuts, though it is not recommended. VNote stores shortcuts' configuration information in the `[shortcuts]` section of user configuration file `vnote.ini`.

For example, the default configruation may look like this:

```ini
[shortcuts]
1\operation=NewNote
1\keysequence=Ctrl+N
2\operation=SaveNote
2\keysequence=Ctrl+S
3\operation=SaveAndRead
3\keysequence=Ctrl+T
4\operation=EditNote
4\keysequence=Ctrl+W
5\operation=CloseNote
5\keysequence=
6\operation=Find
6\keysequence=Ctrl+F
7\operation=FindNext
7\keysequence=F3
8\operation=FindPrevious
8\keysequence=Shift+F3
size=8
```

`size=8` tells VNote that there are 8 shotcuts defined here, with each beginning with the number sequence. You could change the `keysequence` value to change the default key sequence of a specified operation. Leave the `keysequence` empty (`keysequence=`) to disable shortcut for that operation.

Pay attention that `Ctrl+E` is reserved for *Captain Mode* and `Ctrl+Q` is reserved for quitting VNote.

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
Locate to the folder of current note.
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
Within the Captain Mode, `W` will turn VNote into **Navigation Mode**. In this mode, VNote will display at most two characters on some major widgets, and then pressing corresponding characters will jump to that widget.

# Vim Mode
VNote supports a simple but useful Vim mode, including **Normal**, **Insert**, **Visual**, and **VisualLine** modes.

VNote supports following features of Vim:

- `r`, `s`, `S`, `i`, `I`, `a`, `A`, `c`, `C`, `o`, and `O`;
- Actions `d`, `c`, `y`, `p`, `<`, `>`, `gu`, `gU`, `J`, `gJ`, and `~`;
- Movements `h/j/k/l`, `gj/gk`, `Ctrl+U`, `Ctrl+D`, `gg`, `G`, `0`, `^`, `{`, `}`, and `$`;
- Marks `a-z`;
- Registers `"`, `_`, `+`, `a-z`(`A-Z`);
- Jump locations list (`Ctrl+O` and `Ctrl+I`);
- Leader key (`Space`)
    - Currently `<leader>y/d/p` equals to `"+y/d/p`, which will access the system's clipboard;
    - `<leader><Space>` to clear search highlight;
    - `<leader>w` to save note;
- `zz`, `zb`, `zt`;
- `u` and `Ctrl+R` for undo and redo;
- Text objects `i/a`: word, WORD, `''`, `""`, `` ` ` ``, `()`, `[]`, `<>`, and `{}`;
- Command line `:w`, `:wq`, `:x`, `:q`, `:q!`, and `:nohlsearch`;
- Jump between titles
    - `[[`: jump to previous title;
    - `]]`: jump to next title;
    - `[]`: jump to previous title at the same level;
    - `][`: jump to next title at the same level;
    - `[{`: jump to previous title at a higher level;
    - `]}`: jump to next title at a higher level;
- `/` and `?` to search
    - `n` and `N` to find next or previous occurence;
    - `Ctrl+N` and `Ctrl+P` to navigate through the search history;
- `Ctrl+R` to read the content of a register;

For now, VNote does **NOT** support the macro and repeat(`.`) features of Vim.

Enjoy Vim in VNote!
