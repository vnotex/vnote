# External Programs
VNote supports opening notes with external programs. VNote stores external programs' configuration information in the `[external_editors]` section of user configuration file `vnote.ini`.

A sample configuration may look like this:

```ini
[external_editors]
; Define external editors which could be called to edit notes
; One program per line with the format name="program \"%0\" arg1 arg2",<shortcut>
; in which %0 will be replaced with the note file path (so it is better to enclose it
; with double quotes)
; Shortcut could be empty
; Need to escape \ and ", use double quotes to quote paths/arguments with spaces
; SHOULD defined in user config file, not here

GVim=C:\\\"Program Files (x86)\"\\Vim\\vim80\\gvim.exe \"%0\", F4
Notepad=notepad \"%0\"
Notepad%2B%2B=C:\\\"Program Files (x86)\"\\notepad++\\notepad++.exe \"%0\"
```

A restart is needed to detect new external programs.
