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
- `Ctrl+Alt+F`  
Advanced find.
- `Ctrl+Q`  
Quit VNote.
- `Ctrl+J`/`Ctrl+K`  
VNote supports `Ctrl+J` and `Ctrl+K` for navigation in the notebooks list, directories list, notes list, opened notes list, and outline list.
- `Ctrl+Left Mouse`  
Scroll in all directions.
- `Ctrl+Shift+T`  
Recover last closed file.
- `Ctrl+Alt+L`  
Open Flash Page.
- `Ctrl+Alt+I`  
Open Quick Access.
- `Ctrl+T`  
Edit current note or save changes and exit edit mode.
- `Ctrl+G`  
Activate Universal Entry.
- `Ctrl+8`/`Ctrl+9`  
Jump to the next/previous match in last find action.

### Read Mode
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
    - `<N>[[`: jump to previous `N` title;
    - `<N>]]`: jump to next `N` title;
    - `<N>[]`: jump to previous `N` title at the same level;
    - `<N>][`: jump to next `N` title at the same level;
    - `<N>[{`: jump to previous `N` title at a higher level;
    - `<N>]}`: jump to next `N` title at a higher level;
- `/` or `?` to search forward or backward
    - `N`: find next match;
    - `Shift+N`: find previous match;
- `:` for Vim command
    - `:q`: close current note;
    - `:noh[lsearch]`: clear search highlights;

### Edit Mode
- `Ctrl+S`  
Save current changes.
- `Ctrl + +/-`  
Zoom in/out the page.
- `Ctrl+Wheel`  
Zoom in/out the page through the mouse scroll.
- `Ctrl+0`  
Recover the page zoom factor to 100%.
- `Ctrl+J/K`  
Scroll page down/up without changing cursor.
- `Ctrl+N/P`  
Activate auto-completion.
    - `Ctrl+N/P`  
    Navigate through the completion list and insert current completion.
    - `Ctrl+J/K`  
    Navigate through the completion list.
    - `Ctrl+E`  
    Cancel completion.
    - `Enter`  
    Insert current completion.
    - `Ctrl+[` or `Escape`  
    Finish completion.

#### Text Editing
- `Ctrl+B`  
Insert bold. Press `Ctrl+B` again to exit. Current selected text will be changed to bold if exists.
- `Ctrl+I`  
Insert italic. Press `Ctrl+I` again to exit. Current selected text will be changed to italic if exists.
- `Ctrl+D`  
Insert strikethrought. Press `Ctrl+D` again to exit. Current selected text will be changed to strikethrough if exists.
- `Ctrl+;`  
Insert inline code. Press `Ctrl+;` again to exit. Current selected text will be changed to inline code if exists.
- `Ctrl+M`  
Insert fenced code block. Press `Ctrl+M` again to exit. Current selected text will be wrapped into a code block if exists.
- `Ctrl+L`  
Insert link.
- `Ctrl+.`  
Insert table.
- `Ctrl+'`  
Insert image.
- `Ctrl+H`  
Backspace. Delete a character backward.
- `Ctrl+W`  
Delete all the characters from current cursor to the first space backward.
- `Ctrl+U`  
Delete all the characters from current cursor to the beginning of current line.
- `Ctrl+<Num>`  
Insert title at level `<Num>`. `<Num>` should be 1 to 6. Current selected text will be changed to title if exists.
- `Ctrl+7`  
Delete the title mark of current line or selected text.
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

## Customize Shortcuts
VNote supports customizing some standard shortcuts, though it is not recommended. VNote stores shortcuts' configuration information in the `[shortcuts]` and `[captain_mode_shortcuts]` sections of user configuration file `vnote.ini`.

For example, the default configruation may look like this:

```ini
[shortcuts]
; Define shortcuts here, with each item in the form "operation=keysequence".
; Leave keysequence empty to disable the shortcut of an operation.
; Customized shortcuts may conflict with some key bindings in edit mode or Vim mode.
; Ctrl+Q is reserved for quitting VNote.

; Leader key of Captain mode
CaptainMode=Ctrl+E
; Create a note in current folder
NewNote=Ctrl+Alt+N
; Save current note
SaveNote=Ctrl+S
; Close current note
CloseNote=
; Open file/replace dialog
Find=Ctrl+F
; Find next occurence
FindNext=F3
; Find previous occurence
FindPrevious=Shift+F3

[captain_mode_shortcuts]
; Define shortcuts in Captain mode here.
; There shortcuts are the sub-sequence after the CaptainMode key sequence
; in [shortcuts].

; Enter Navigation mode
NavigationMode=W
; Show attachment list of current note
AttachmentList=A
; Locate to the folder of current note
LocateCurrentFile=D
; Toggle Expand mode
ExpandMode=E
; Alternate one/two panels view
OnePanelView=P
; Discard changes and enter read mode
DiscardAndRead=Q
; Toggle Tools dock widget
ToolsDock=T
; Close current note
CloseNote=X
; Show shortcuts help document
ShortcutsHelp=Shift+?
; Flush the log file
FlushLogFile=";"
; Show opened files list
OpenedFileList=F
; Activate the ith tab
ActivateTab1=1
ActivateTab2=2
ActivateTab3=3
ActivateTab4=4
ActivateTab5=5
ActivateTab6=6
ActivateTab7=7
ActivateTab8=8
ActivateTab9=9
; Alternate between current and last tab
AlternateTab=0
; Activate next tab
ActivateNextTab=J
; Activate previous tab
ActivatePreviousTab=K
; Activate the window split on the left
ActivateSplitLeft=H
; Activate the window split on the right
ActivateSplitRight=L
; Move current tab one split left
MoveTabSplitLeft=Shift+H
; Move current tab one split right
MoveTabSplitRight=Shift+L
; Create a vertical split
VerticalSplit=V
; Remove current split
RemoveSplit=R
```

Each item is in the form `operation=keysequence`, with `keysequence` empty to disable shortcuts for that operation.

Pay attention that `Ctrl+Q` is reserved for quitting VNote.

# Captain Mode
To efficiently utilize the shortcuts, VNote supports the **Captain Mode**.

Press the leader key `Ctrl+E`, then VNote will enter the Captain Mode, within which VNote supports more efficient shortcuts.

- `E`   
Toggle expanding the edit area.
- `Y`   
Focus to the edit area.
- `T`   
Toggle the Tools panel.
- `Shift+#`   
Toggle the Tool Bar.
- `F`   
Popup the opened notes list of current split window. Within this list, pressing the sequence number in front of each note could jump to that note.
- `A`   
Popup the attachments list of current note.
- `X`   
Close current tab.
- `J`   
Jump to the next tab.
- `K`   
Jump to the previous tab.
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
- `Shift+|`   
Maximize current split window.
- `=`   
Distribute all the split windows evenly.
- `H`   
Jump to the first split window on the left.
- `L`   
Jump to the first split window on the right.
- `Shift+H`   
Move current tab one split window left.
- `Shift+L`  
Move current tab one split window right.
- `M`  
Evaluate current cursor word or selected text as magic words.
- `S`  
Apply a snippet in edit mode.
- `O`  
Export notes.
- `I`  
Toggle live preview panel.
- `U`  
Expand/restore live preview panel.
- `C`  
Toggle full-text search.
- `P`  
Parse HTML as Markdown text in clipboard and paste.
- `N`  
View and edit current note's information.
- `Shift+?`   
Display shortcuts documentation.

## Navigation Mode
Within the Captain Mode, `W` will turn VNote into **Navigation Mode**. In this mode, VNote will display at most two characters on some major widgets, and then pressing corresponding characters will jump to that widget.

# Vim Mode
VNote supports a simple but useful Vim mode, including **Normal**, **Insert**, **Visual**, and **VisualLine** modes.

::: alert-info

Open `Settings` dialog via the `File` menu, then jump to `Read/Edit` tab and you could enable Vim via the combo box in `Key mode`. Restart VNote to take effect.

:::

VNote supports following features of Vim:

- `r`, `s`, `S`, `i`, `I`, `a`, `A`, `c`, `C`, `o`, and `O`;
- Actions `d`, `c`, `y`, `p`, `<`, `>`, `gu`, `gU`, `J`, `gJ`, and `~`;
- Movements `h/j/k/l`, `gj/gk/g0`, `Ctrl+U`, `Ctrl+D`, `gg`, `G`, `0`, `^`, `{`, `}`, and `$`;
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
- Command line `:w`, `:wq`, `:x`, `:q`, `:q!`, and `:noh[lsearch]`;
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
- `Ctrl+O` in Insert mode to enter Normal mode temporarily;

For now, VNote does **NOT** support the macro and repeat(`.`) features of Vim.

Enjoy Vim in VNote!

# Others
- `Ctrl+J` and `Ctrl+K` to navigate through items;
- `Ctrl+N` and `Ctrl+P` to navigate through search results in list;
