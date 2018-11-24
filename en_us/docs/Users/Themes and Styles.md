# Themes and Styles
## Themes
A **theme** specifies the look of VNote, the style of the editor and read mode, and the syntax highlight style of code block.

A theme corresponds to a folder in the `themes` subfolder in the configuration folder with structure:

```
themes/
├── v_pure
│   ├── arrow_dropdown.svg
│   ├── branch_closed.svg
│   ├── branch_end.svg
│   ├── branch_more.svg
│   ├── branch_open.svg
│   ├── checkbox_checked.svg
│   ├── checkbox_unchecked.svg
│   ├── close_grey.svg
│   ├── close.svg
│   ├── down_disabled.svg
│   ├── down.svg
│   ├── float.svg
│   ├── left_disabled.svg
│   ├── left.svg
│   ├── line.svg
│   ├── menu_checkbox.svg
│   ├── menu_radiobutton.svg
│   ├── radiobutton_checked.svg
│   ├── radiobutton_unchecked.svg
│   ├── right_disabled.svg
│   ├── right.svg
│   ├── up_disabled.svg
│   ├── up.svg
│   ├── v_pure_codeblock.css
│   ├── v_pure.css
│   ├── v_pure.mdhl
│   ├── v_pure.palette
│   └── v_pure.qss
```

- `v_pure.palette`: the main entry of a theme, which specifies other files and styles;
- `v_pure.qss`: file for [**Qt Style Sheet**](http://doc.qt.io/qt-5/stylesheet-reference.html), which specifies the look of all the widgets;
- `v_pure.mdhl`: file for the style of Markdown editor, using [The Syntax of PEG Markdown Highlight Stylesheets](http://hasseg.org/peg-markdown-highlight/docs/stylesheet_syntax.html);
- `v_pure.css`: file for the style of read mode;
- `v_pure_codeblock.css`: file for the style of the code block highlight in read mode, using [Highlight.js](https://highlightjs.org/static/demo/);

### Palette of Theme
Let's look into the `.palette` file. The file is in `INI` format.

#### [metadata]
This section specifies other style files this theme will use.

```ini
; File path could be absolute path or relative path (related to this file).
; Use @color_tag to reference a style.

[metadata]
qss_file=v_pure.qss
mdhl_file=v_pure.mdhl
css_file=v_pure.css
codeblock_css_file=v_pure_codeblock.css
version=2
```

#### [phony]
This section is used to define **variables** for fundamental, abstract color attributes. A variable could be referenced by `@variable_name` to define another variable.

These variables are referenced by other sections, so you are free to choose and define your own ones.

```ini
[phony]
; Abstract color attributes.
master_fg=#F5F5F5
master_bg=#00897B
master_light_bg=#80CBC4
master_dark_bg=#00796B
master_focus_bg=#009688
master_hover_bg=#009688
master_pressed_bg=#00796B

base_fg=#222222
base_bg=#EAEAEA

main_fg=@base_fg
main_bg=@base_bg

title_fg=@base_fg
title_bg=@base_bg

disabled_fg=#9E9E9E

content_fg=@base_fg
content_bg=@base_bg

border_bg=#D3D3D3

separator_bg=#D3D3D3

hover_fg=@base_fg
hover_bg=#D0D0D0

selected_fg=@base_fg
selected_bg=#BDBDBD

active_fg=@selected_fg
active_bg=@selected_bg

inactive_fg=@selected_fg
inactive_bg=#D3D3D3

focus_fg=@selected_fg
focus_bg=@selected_bg

pressed_fg=@base_fg
pressed_bg=#B2B2B2

edit_fg=#222222
edit_bg=#F5F5F5
edit_focus_bg=#E0F2F1
edit_focus_border=@master_bg
edit_selection_fg=@edit_fg
edit_selection_bg=@master_light_bg

icon_fg=#222222
icon_disabled_fg=@disabled_fg

danger_fg=#F5F5F5
danger_bg=#C9302C
danger_focus_bg=#D9534F
danger_hover_bg=#D9534F
danger_pressed_bg=#AC2925
```

#### [soft_defined]
This section define variables which will be used by VNote code. You **MUST** define these variables to make VNote looks right.

```ini
[soft_defined]
; VAvatar.
; The foreground color of the avatar when Captain mode is triggered.
avatar_captain_mode_fg=@master_fg
; The background color of the avatar when Captain mode is triggered.
avatar_captain_mode_bg=@master_bg

; Style of the label in Navigation mode.
navigation_label_fg=@master_fg
navigation_label_bg=@master_bg

; Style of the bubble of VButtonWithWidget.
bubble_fg=@master_fg
bubble_bg=@master_bg

; Icons' foreground.
danger_icon_fg=@danger_bg
item_icon_fg=@icon_fg
title_icon_fg=@icon_fg

; VVimIndicator.
vim_indicator_key_label_fg=@base_fg
vim_indicator_mode_label_fg=@base_fg
vim_indicator_cmd_edit_pending_bg=@selected_bg

; VTabIndicator.
tab_indicator_label_fg=@base_fg

; Html template.
template_title_flash_light_fg=@master_light_bg
template_title_flash_dark_fg=@master_bg

; Search hit items in list or tree view.
search_hit_item_fg=@selected_fg
search_hit_item_bg=@master_light_bg
```

#### [widgets]
This section defines variables to be used in `qss` file to define concrete style of different widgets. They are referenced by the `qss` file.

```ini
[widgets]
; Widget color attributes.

; QWidget.
widget_fg=@base_fg

; Separator of dock widgets.
dock_separator_bg=@border_bg
dock_separator_hover_bg=@border_bg

; Menubar.
menubar_bg=@main_bg
menubar_fg=@main_fg
menubar_item_selected_bg=@selected_bg

; Menu.
menu_bg=@base_bg
menu_fg=@base_fg
menu_item_disabled_fg=@disabled_fg
menu_item_selected_fg=@selected_fg
menu_item_selected_bg=@selected_bg
menu_separator_bg=@separator_bg
menu_icon_fg=@icon_fg
menu_icon_danger_fg=@danger_icon_fg
```

The `qss` file may look like this if you are curious:

```css
/* QWidget */
QWidget
{
    color: @widget_fg;
    font-family: "Hiragino Sans GB", "冬青黑体", "Microsoft YaHei", "微软雅黑", "Microsoft YaHei UI", "WenQuanYi Micro Hei", "文泉驿雅黑", "Dengxian", "等线体", "STXihei", "华文细黑", "Liberation Sans", "Droid Sans", "NSimSun", "新宋体", "SimSun", "宋体", "Helvetica", "sans-serif", "Tahoma", "Arial", "Verdana", "Geneva", "Georgia", "Times New Roman";
}

QWidget[NotebookPanel="true"] {
    padding-left: 3px;
}
/* End QWidget */

/* QMainWindow */
QMainWindow {
    color: @base_fg;
    background: @base_bg;
}

QMainWindow::separator {
    background: @dock_separator_bg;
    width: 2px;
    height: 2px;
}

QMainWindow::separator:hover {
    background: @dock_separator_hover_bg;
}
/* End QMainWindow */

QMenuBar {
    border: none;
    background: @menubar_bg;
    color: @menubar_fg;
}

QMenuBar::item:selected {
    background: @menubar_item_selected_bg;
}
```

### Custom Themes
VNote supports custom themes. Just place your theme (folder) to the `themes` folder, restart VNote and choose your theme in the `File` menu.

The best way to custom a theme is tuning a defaut theme. VNote will output default themes in the `themes` folder (or if not, you could download it [here](https://github.com/tamlok/vnote/tree/master/src/resources/themes)). Copy a default theme and rename the `palette` file. Then you could tune it to your taste. Restart of VNote is needed to let the changes take effect.

## Editor Styles
Editor style is specified by a `mdhl` file. Each theme may carry a `mdhl` file. You could also apply another `mdhl` file instead of using the default one specified by the theme. Separate style file could be placed in the `styles` folder in the configuration folder. Restart of VNote is needed to scan new `mdhl` files and re-open of notes is needed to apply new style.

![](_v_images/_1517715930_350213329.png)

### Syntax of MDHL File
The `mdhl` file adopts the [The Syntax of PEG Markdown Highlight Stylesheets](http://hasseg.org/peg-markdown-highlight/docs/stylesheet_syntax.html) and expands it to support more elements.

```
# This is the default markdown styles used for Peg-Markdown-Highlight
# created by Le Tan(tamlokveer@gmail.com).
# For a complete description of the syntax, please refer to the original
# documentation of the style parser
# [The Syntax of PEG Markdown Highlight Stylesheets](http://hasseg.org/peg-markdown-highlight/docs/stylesheet_syntax.html).
# VNote adds some styles in the syntax which will be marked [VNote] in the comment.
#
# Note: Empty lines within a section is NOT allowed.
# Note: Do NOT modify this file directly. Copy it and tune your own style!

editor
# QTextEdit just choose the first available font, so specify the Chinese fonts first
# Do not use "" to quote the name
font-family: Hiragino Sans GB, 冬青黑体, Microsoft YaHei, 微软雅黑, Microsoft YaHei UI, WenQuanYi Micro Hei, 文泉驿雅黑, Dengxian, 等线体, STXihei, 华文细黑, Liberation Sans, Droid Sans, NSimSun, 新宋体, SimSun, 宋体, Helvetica, sans-serif, Tahoma, Arial, Verdana, Geneva, Georgia, Times New Roman
font-size: 12
foreground: 222222
background: f5f5f5
# [VNote] Style for trailing space
trailing-space: a8a8a8
# [VNote] Style for line number
line-number-background: eaeaea
line-number-foreground: 424242
# [VNote] Style for selected word highlight
selected-word-foreground: 222222
selected-word-background: dfdf00
# [VNote] Style for searched word highlight
searched-word-foreground: 222222
searched-word-background: 4db6ac
# [VNote] Style for searched word under cursor highlight
searched-word-cursor-foreground: 222222
searched-word-cursor-background: 66bb6a
# [VNote] Style for incremental searched word highlight
incremental-searched-word-foreground: 222222
incremental-searched-word-background: ce93d8
# [VNote] Style for color column in fenced code block
color-column-background: dd0000
color-column-foreground: ffff00
# [VNote} Style for preview image line
preview-image-line-foreground: 9575cd

editor-selection
foreground: eeeeee
background: 005fff

editor-current-line
background: c5cae9
# [VNote] Vim insert mode cursor line background
vim-insert-background: c5cae9
# [VNote] Vim normal mode cursor line background
vim-normal-background: e0e0e0
# [VNote] Vim visual mode cursor line background
vim-visual-background: bbdefb
# [VNote] Vim replace mode cursor line background
vim-replace-background: f8bbd0

H1
foreground: 222222
font-style: bold
font-size: +8

VERBATIM
foreground: 673ab7
font-family: Consolas, Monaco, Andale Mono, Monospace, Courier New
# [VNote] Codeblock sylte from HighlightJS (bold, italic, underlined, color)
# The last occurence of the same attribute takes effect
# Could specify multiple attribute in one line
hljs-comment: 6c6c6c
hljs-keyword: 0000ee
hljs-attribute: 0000ee
hljs-selector-tag: 0000ee
hljs-meta-keyword: 0000ee
hljs-doctag: 0000ee
hljs-name: 0000ee
hljs-type: 880000
hljs-string: 880000
hljs-number: 880000
hljs-selector-id: 880000
hljs-selector-class: 880000
hljs-quote: 880000
hljs-template-tag: 880000
hljs-deletion: 880000
hljs-title: bold, 880000
hljs-section: bold, 880000
hljs-regexp: bc6060
hljs-symbol: bc6060
hljs-variable: bc6060
hljs-template-variable: bc6060
hljs-link: bc6060
hljs-selector-attr: bc6060
hljs-selector-pseudo: bc6060
hljs-literal: af00d7
hljs-built_in: 008700
hljs-bullet: 008700
hljs-code: 008700
hljs-addition: 008700
hljs-meta: 1f7199
hljs-meta-string: 4d99bf
hljs-emphasis: italic
hljs-strong: bold
```

The syntax highlight of the code blocks in edit mode is specified by the `VERBATIM` element.

For example, if you want to change the font size of the code block in edit mode, you may need to add the following line under `VERBATIM` element:

```
font-size: -2
```

## Rendering Styles
Rendering style in read mode is specified by a `css` file. Each theme may carry a `css` file. You could also apply another `css` file instead of using the default one specified by the theme. Separate style file could be placed in the `styles` folder in the configuration folder. Restart of VNote is needed to scan new `css` files and re-open of notes is needed to apply new style.

![](_v_images/_1517717905_885171283.png)

## Code Block Rendering Styles
Rendering style in read mode is specified by a `css` file. Each theme may carry a `css` file. You could also apply another `css` file instead of using the default one specified by the theme. Separate style file could be placed in the `styles/codeblock_styles` folder in the configuration folder. Restart of VNote is needed to scan new `css` files and re-open of notes is needed to apply new style.

This file is used by the **highlight.js** renderer engine. You could download existing style files from [Highlight.js Github](https://github.com/isagalaev/highlight.js/tree/master/src/styles).