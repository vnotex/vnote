
The Syntax of PEG Markdown Highlight Stylesheets
================================================

[PEG Markdown Highlight][pmh] includes a parser for stylesheets that define how different Markdown language elements are to be highlighted. This document describes the syntax of these stylesheets.

[pmh]: http://hasseg.org/peg-markdown-highlight/


Example
-------

Here is a quick, simple example of a stylesheet:

<style>
.codetable { border-collapse: collapse; }
.codetable .left { text-align: right; padding-right: 10px; }
.codetable .right { text-align: left; padding-left: 10px; }
.codetable .content { font-family: monospace; background: #eee; padding: 0 5px; }
.codetable .comment { color: #174EB3; }
.codetable .rule { color: #491B8F; }
.codetable .attrname { color: #48B317; }
.codetable .attrvalue { color: #A65C1F; }
</style>

<table class="codetable">
<tr>
    <td class="left"></td>
    <td class="content"># The first comment lines</td>
    <td class="right"></td>
</tr>
<tr>
    <td class="left"></td>
    <td class="content"># describe the stylesheet.</td>
    <td class="right"></td>
</tr>
<tr>
    <td class="left"></td>
    <td class="content">&nbsp;</td>
    <td class="right"></td>
</tr>
<tr>
    <td class="left"><span class="rule">Style rule &rarr;</span></td>
    <td class="content"><span class="rule">editor:</span></td>
    <td class="right"></td>
</tr>
<tr>
    <td class="left"></td>
    <td class="content">&nbsp;&nbsp;foreground: ff0000 <span class="comment"># red text</span></td>
    <td class="right"><span class="comment">&larr; Comment</span></td>
</tr>
<tr>
    <td class="left"><span class="attrname">Attribute name &rarr;</span></td>
    <td class="content">&nbsp;&nbsp;<span class="attrname">font-family</span>: <span class="attrvalue">Consolas</span></td>
    <td class="right"><span class="attrvalue">&larr; Attribute value</span></td>
</tr>
<tr>
    <td class="left"></td>
    <td class="content">&nbsp;</td>
    <td class="right"></td>
</tr>
<tr>
    <td class="left"></td>
    <td class="content">EMPH:</td>
    <td class="right"></td>
</tr>
<tr>
    <td class="left"></td>
    <td class="content">&nbsp;&nbsp;font-size: 14</td>
    <td class="right"></td>
</tr>
<tr>
    <td class="left"></td>
    <td class="content">&nbsp;&nbsp;font-style: bold, underlined</td>
    <td class="right"></td>
</tr>
</table>


Style Rules
-----------

A stylesheet is composed of one or more *rules*. Rules are separated from each other by **empty lines** like so:

    H2:
    foreground: ff0000
    
    H3:
    foreground: 00ff00

Each begins with the ***name* of the rule**, which is always on its own line, and may be one of the following:

- **`editor`**: Styles that apply to the whole document/editor
- **`editor-current-line`**: Styles that apply to the current line in the editor (i.e. the line where the caret is)
- **`editor-selection`**: Styles that apply to the selected range in the editor when the user makes a selection in the text
- A Markdown element type (like `EMPH`, `REFERENCE` or `H1`): Styles that apply to occurrences of that particular element. The supported element types are:
    - **`LINK`:** Explicit link (like `[click here][ref]`)
    - **`AUTO_LINK_URL`:** Implicit URL link (like `<http://google.com>`)
    - **`AUTO_LINK_EMAIL`:** Implicit email link (like `<first.last@google.com>`)
    - **`IMAGE`:** Image definition
    - **`REFERENCE`:** Reference (like `[id]: http://www.google.com`)
    - **`CODE`:** Inline code
    - **`EMPH`:** Emphasized text
    - **`STRONG`:** Strong text
    - **`LIST_BULLET`:** Bullet for an unordered list item
    - **`LIST_ENUMERATOR`:** Enumerator for an ordered list item
    - **`H1`:** Header, level 1
    - **`H2`:** Header, level 2
    - **`H3`:** Header, level 3
    - **`H4`:** Header, level 4
    - **`H5`:** Header, level 5
    - **`H6`:** Header, level 6
    - **`BLOCKQUOTE`:** Blockquote marker
    - **`VERBATIM`:** Block of code
    - **`HRULE`:** Horizontal rule
    - **`HTML`:** HTML tag
    - **`HTML_ENTITY`:** HTML special entity definition (like `&hellip;`)
    - **`HTMLBLOCK`:** Block of HTML
    - **`COMMENT`:** (HTML) Comment
    - **`NOTE`:** Note
    - **`STRIKE`:** Strike-through

The name may be optionally followed by an assignment operator (either `:` or `=`):

    H1:
    foreground: ff00ff
    
    H2 =
    foreground: ff0000
    
    H3
    foreground: 00ff00

The **order of style rules is significant**; it defines the order in which different language elements should be highlighted. *(Of course applications that use PEG Markdown Highlight and the style parser may disregard this and highlight elements in whatever order they desire.)*

After the name of the rule, there can be one or more *attributes*.


Style Attributes
----------------

Attribute assignments are each on their own line, and they consist of the *name* of the attribute as well as the *value* assigned to it. An assignment operator (either `:` or `=`) separates the name from the value:

    attribute-name: value
    attribute-name= value

Attribute assignment lines **may be indented**.

### Attribute Names and Types

The following is a list of the names of predefined attributes, and the values they may be assigned:

- `foreground-color` *(aliases: `foreground` and `color`)*
    - See the *Color Attribute Values* subsection for information about valid values for this attribute.
- `background-color` *(alias: `background`)*
    - See the *Color Attribute Values* subsection for information about valid values for this attribute.
- `caret-color` *(alias: `caret`)*
    - See the *Color Attribute Values* subsection for information about valid values for this attribute.
- `strike-color` *(alias: `strike`)*
    - See the *Color Attribute Values* subsection for information about valid values for this attribute.
- `font-size`
    - An integer value for the font size, *in points* (i.e. not in pixels). The number may have a textual suffix such as `pt`.
    - If the value begins with `+` or `-`, it is considered *relative* to some base font size (as defined by the host application). For example, the value `3` defines the font size as 3 (absolute) while `+3` defines it as +3 (relative), i.e. 3 point sizes larger than the base font size.
- `font-family`
    - A comma-separated list of one or more arbitrary font family names. *(It is up to the application that uses the PEG Markdown Highlight library to resolve this string to actual fonts on the system.)*
- `font-style`
    - A comma-separated list of one or more of the following:
        - `italic`
        - `bold`
        - `underlined`

Applications may also include support for any **custom attribute names and values** they desire &mdash; attributes other than the ones listed above will be included in the style parser results, with their values stored as strings.


## Color Attribute Values

Colors can be specified either in **RGB** (red, green, blue) or **ARGB** (alpha, red, green, blue) formats. In both, each component is a two-character hexadecimal value (from `00` to `FF`):

    foreground: ff00ee  # red = ff, green = 00, blue = ee (and implicitly, alpha = ff)
    background: 99ff00ee  # alpha = 99, red = ff, green = 00, blue = ee


Comments
--------

Each line in a stylesheet may have a comment. The `#` character begins a line comment that continues until the end of the line:

    # this line has only this comment
    H1:  # this line has a style rule name and then a comment
    foreground: ff0000  # this line has an attribute and then a comment









