# Markdown Guide
This is a quick guide [^1] for Markdown, a lightweight and easy-to-use syntax for writing.

## What is Markdown?
Markdown is a way to style text via a few simple marker characters. You could write the document in plain text and then read it with a beautiful typesetting.

There is no a standard Markdown syntax and many editor will add its own additional syntax. VNote supports only the widely-used basic syntax.

## How to Use Markdown?
If you are new to Markdown, it is better to learn the syntax elements step by step. Knowing headers and emphasis is enough to make you survive. You could learn another new syntax and practise it every one or two days.

## Syntax Guide
Here is an overview of Markdown syntax supported by VNote.

### Headers
```md
# This is an <h1> tag
## This is an <h2> tag
###### This is an <h6> tag
```

**Notes**:

- At least one space is needed after the `#`;
- A header should occupy one entire line;

### Emphasis
```md
*This text will be italic*  
_This text will be italic_  

**This text will be bold**  
__This text will be bold__
```

**Notes**:

- `*` is recommended in VNote;
- If the render failed, try to add an additional space before the first `*` and after the last `*`. The space is necessary if the surrounded text begins or ends with full width punctuation;
- VNote provides shortcuts `Ctrl+I` and `Ctrl+B` to insert italic and bold text;

### Lists
#### Unordered
```md
* Item 1  
This is a text under Item 1. Notice that there are two spaces at the end above.
* Item 2
    * Item 2a
    * Item 2b
* Item 3

To end a list, there should be one empty line above.
```

#### Ordered
```md
1. Item 1
1. Item 2  
Notice that the sequence number is irrelevant. Markdown will change the sequence automatically when renderring.
3. Item 3
    1. Item 3a
    2. Item 3b
4. Item 4
```

### Tables
```md
| col 1 | col 2 | col 3 |
| --- | --- | --- |
| cell1 | cell2 | cell3 |
| cell4 | cell5 | cell6 |
```

### Images and Links
```md
![Image Alt Text](/url/to/images.png)

[Link Text](/url/of/the/link)
```

**Notes**:

- It is not recommended to use image links in reference format. VNote will not preview those images.

### Blockquotes
```md
As VNote suggests:

> VNote is the best Markdown note-taking application
> ever.  
>
> THere is two spaces after `ever.` above to insert a
> new line.
```

**Notes**:

- Space is needed after the marker `>`;
- You could just add only one `>` at the first line;

### Fenced Code Block
    ```lang
    This is a fenced code block.
    ```

**Notes**:

- `lang` is optional to specify the language of the code;

### Inline Code
```md
Here is a `inline code`.
```

### Strikethrough
```md
Here is a ~~text~~ with strikethrough.
```

**Notes**:

- VNote provides shortcuts `Ctrl+D` to insert text with strikethrough;

### Task Lists
```md
- [x] this is a complete item.
- [ ] this is an incomplete item.
```

### Subscript and Superscript
```md
This is a text with subscript H~2~o.

This is a text with superscript 29^th^.
```

### Footnote
```md
This is a footnote [^1].

[^1]: Here is the detail of the footnote.
```

### New Line and Paragraph
If you want to enter a new line, you should add two spaces after current line and then continue your input. VNote provides `Shift+Enter` to help.

If you want to enter a new paragraph, you should add an empty line and then continue entering the new paragraph.

Generally, you need to add an empty line after a block element (such as code block, lists, blockquote) to explicitly end it.

[^1]: This guide references [Mastering Markdown](https://guides.github.com/features/mastering-markdown/).
