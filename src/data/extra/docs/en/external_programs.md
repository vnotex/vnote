# External Programs
VNote allows user to open notes with **external programs** via the `Open With` in the context menu of the node explorer.

To add custom external programs, user needs to edit the session configuration. A sample may look like this:

```json
{
    "external_programs": [
        {
            "name" : "gvim",
            "command" : "C:\\\"Program Files (x86)\"\\Vim\\vim80\\gvim.exe %1",
            "shortcut" : "F4"
        },
        {
            "name" : "notepad",
            "command" : "notepad %1",
            "shortcut" : ""
        }
    ]
}
```

An external program could have 3 properties:

1. `name`: the name of the program in VNote;
2. `command`: the command to execute when opening notes with this external program;
    1. Use `%1` as a placeholder which will be replaced by the real file paths (automatically wrapped by double quotes);
3. `shortcut`: the shortcut assigned to this external program;

Close VNote before editting the session configuration.
