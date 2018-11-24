# Full-Text Search
VNote provides powerful built-in full-text search to search your notes.

`Ctrl+E C` or `Ctrl+Alt F` to activate the **Search** dock widget. You could also activate it through the `Edit` or `View` menu.

![](_v_images/_1527406007_1635981025.png)

## Keywords
- Specify the keywords to search for;
- Support `&&` and `||` for AND and OR logics, such as `markdown && vnote`;
- Space-separated keywords mean AND, such as `markdown vnote`;

### Magic Switch
VNote supports **Magic Switch** in the keywords to turn on/off some options of the search:

- `\f` or `\F`: disable or enable **Fuzzy Search**;
- `\c` or `\C`: case insensitive or sensitive;
- `\r` or `\R`: disable or enable **Regular Expression**;
- `\w` or `\W`: disable or enable **Whole Word Only**;

Example: `vnote \C \W` to search `vnote` with case-insensitive and whole-word-only.

## Scope
There are four scopes to constrain the search:

- `Opened Notes`;
- `Current Folder`;
- `Current Notebook`;
- `All Notebooks`;

## Object
Specify whether the search should be executed against the **Content** or the **Name**.

## Target
We could search among:

- `Note`;
- `Folder`;
- `Notebook`;
- `Note/Folder/Notebook`;

## File Pattern
We could specify the file pattern to filter the files we are interested in, such as `*.md` to search only Markdown files.