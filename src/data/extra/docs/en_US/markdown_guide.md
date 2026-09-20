# Markdown Guide
This is a quick guide [^1] for Markdown, a lightweight and easy-to-use syntax for writing.

## What is Markdown?
Markdown is a way to style text via a few simple markers. You could write the document in plain text and then read it with a beautiful typesetting.

Markdown editors may support different extensions. VNote supports common Markdown syntax and the extensions introduced below.

## How to Use Markdown?
If you are new to Markdown, it is better to learn the syntax elements step by step. Knowing headers and emphasis is enough to survive. You could learn another new syntax and practise it every one or two days.

## Syntax Guide
Here is an overview of Markdown syntax supported by VNote.

### Headers
```md
# This is a <h1> tag
## This is a <h2> tag
###### This is a <h6> tag
```

**Notes**:

* At least one space is needed after the `#`
* A header should occupy one entire line

### Emphasis
```md
*This text will be italic*
_This text will be italic_

**This text will be bold**
__This text will be bold__
```

**Notes**:

* `*` is recommended in VNote
* If the render failed, try to add an additional space before the first `*` and after the last `*`. The space is necessary if the surrounded text begins or ends with full width punctuation

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
The first number determines where the list starts. Markdown numbers the following items automatically when rendering.
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
![Image Alt Text](/url/to/image.png "Optional Text")

[Link Text](/url/of/the/link)
```

**Notes**:

* Reference-style image links are also supported, including editor previews when image previews are enabled. For example, use `![Image Alt Text][image]` with a separate definition line `[image]: /url/to/image.png`.

### Blockquotes
```md
As VNote suggests:

> VNote is the best Markdown note-taking application
> ever.  
>
> There are two spaces after `ever.` above to insert a
> new line.

It also suggests:

> VNote is good.  
Here is another sentence within the quote.
```

**Notes**:

* A space after the marker `>` is recommended for readability
* For a continued paragraph, you can omit `>` on subsequent lines

### Fenced Code Block
    ```lang
    This is a fenced code block.
    ```

    ~~~cpp
    This is another fenced code block.
    ~~~

**Notes**:

* `lang` is optional to specify the language of the code; if not specified, VNote won't highlight the code
    * For a detailed supported languages list, please visit [Prism](https://prismjs.com/#supported-languages)
* It is always a good practice to add one empty line before the whole fenced code block

### Diagrams
VNote supports the following engines to draw diagrams. You should specify particular language of the fenced code block and write the definition of your diagram within it.

* [Flowchart.js](http://flowchart.js.org/) for *flowchart* with language `flow` or `flowchart`
* [Mermaid](https://github.com/mermaid-js/mermaid) with language `mermaid`
* [WaveDrom](https://wavedrom.com/) for *digital timing diagram* with language `wavedrom`

For example,

    ```flowchart
    st=>start: Start:>http://www.google.com[blank]
    e=>end:>http://www.google.com
    op1=>operation: My Operation
    sub1=>subroutine: My Subroutine
    cond=>condition: Yes
    or No?:>http://www.google.com
    io=>inputoutput: catch something...

    st->op1->cond
    cond(yes)->io->e
    cond(no)->sub1(right)->op1
    ```

#### UML
VNote supports [PlantUML](http://plantuml.com/) to draw UML diagrams. You should use `puml` specified as the language of the fenced code block and write the definition of your diagram within it.

    ```puml
    @startuml
    Bob -> Alice : hello
    @enduml
    ```

#### Graphviz
VNote supports [Graphviz](http://www.graphviz.org/) to draw diagrams. You should use `dot` specified as the language of the fenced code block and write the definition of your diagram within it.

### Math Formulas
VNote renders math formulas with [KaTeX](https://katex.org/) by default; [MathJax](https://www.mathjax.org/) is an optional renderer. The default math delimiters are `$$...$$` for **displayed mathematics**, and `$...$` for **inline mathematics**.

* Inline mathematics should not cross multiple lines
* Forms like `3$abc$`, `$abc$4`, `$ abc$`, and `$abc $` will not be treated as mathematics
* Use `\` to escape `$`
* For a multiline formula, put the opening and closing `$$` on their own lines; an equation number may follow the closing delimiter
* Use `\\` inside an `aligned` environment to split a displayed formula into lines

VNote also supports displayed mathematics via a fenced code block with language `mathjax`. This fence uses the selected math renderer, including KaTeX.

    ```mathjax
    $$
    J(\theta) = \frac 1 2 \sum_{i=1}^m (h_\theta(x^{(i)})-y^{(i)})^2
    $$
    ```

Equation numbers for displayed mathematics are supported:

    $$vnote x markdown = awesome$$ (1.2.1)

### Inline Code
```md
Here is some `inline code`.
```

To insert one `` ` ``, you need to use two `` ` `` to enclose it, such as ``` `` ` `` ```. To insert two `` ` ``, you need to use three `` ` ``.

### Strikethrough
```md
Here is a ~~text~~ with strikethrough.
```

### Task Lists
```md
* [x] this is a complete item.
* [ ] this is an incomplete item.
```

### Footnote
```md
This is a footnote [^1].

[^1]: Here is the detail of the footnote.
```

### Superscript and Subscript
```md
This is the 1^st^ superscript.

This is the H~2~O subscript.
```

### Mark
```md
Let's mark the ==word==.
```

### Alert
```md
::: alert-info

This is an info text.

:::

::: alert-danger

This is a danger text.

:::
```

Available variants:

```
alert-primary
alert-secondary
alert-success
alert-info
alert-warning
alert-danger
alert-light
alert-dark
```

### New Line and Paragraph
If you want to enter a new line, you should add two spaces after current line and then continue your input. VNote provides `Shift+Enter` to help.

If you want to enter a new paragraph, you should add an empty line and then continue entering the new paragraph.

Generally, you need to add an empty line after a block element (such as code block, lists, blockquote) to explicitly end it.

[^1]: This guide references [Mastering Markdown](https://guides.github.com/features/mastering-markdown/).
