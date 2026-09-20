# Markdown指南
Markdown是一种轻量级的易用的书写语法。本文是Markdown的一个快速指南[^1]。

## 什么是Markdown？
Markdown是一种通过少量简单的标记字符来格式化文本的方法。您可以用纯文本来书写文档，然后在阅读时呈现一个美观的排版。

不同的Markdown编辑器可能支持不同的扩展语法。VNote支持常用的Markdown语法和下文介绍的扩展语法。

## 如何上手Markdown？
如果刚接触Markdown，那么比较好的一个方法是逐个学习Markdown语法。刚开始，懂得标题和强调语法就能够写出基本的文档；然后，每天可以学习一个新的语法并不断练习。

## 语法指南
下面是VNote支持的Markdown语法的一个概览。

### 标题
```md
# This is a <h1> tag
## This is a <h2> tag
###### This is a <h6> tag
```

**注意**：

* `#`之后需要至少一个空格
* 一个标题应该占一整行

### 强调
```md
*This text will be italic*
_This text will be italic_

**This text will be bold**
__This text will be bold__
```

**注意**：

* VNote推荐使用`*`
* 如果渲染错误，请尝试在第一个`*`之前以及最后一个`*`之后添加一个空格。如果被标记的文本是以全角符号开始或结尾，一般都需要前后添加一个空格

### 列表
#### 无序列表
```md
* Item 1  
这是一段在Item 1下面的文字。需要注意上面一行结尾有两个空格。
* Item 2
    * Item 2a
    * Item 2b
* Item 3

使用一个空行来结束一个列表。
```

#### 有序列表
```md
1. Item 1
1. Item 2  
第一个序号决定列表的起始编号，渲染时Markdown会自动为后续项目编号。
3. Item 3
    1. Item 3a
    2. Item 3b
4. Item 4
```

### 表格
```md
| col 1 | col 2 | col 3 |
| --- | --- | --- |
| cell1 | cell2 | cell3 |
| cell4 | cell5 | cell6 |
```

### 图片和链接
```md
![Image Alt Text](/url/to/image.png "Optional Text")

[Link Text](/url/of/the/link)
```

**注意**：

* VNote也支持参考式的图片链接，并可在启用图片预览时在编辑器中预览。例如，使用`![Image Alt Text][image]`，并另起一行定义`[image]: /url/to/image.png`。

### 块引用
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

**注意**：

* 建议在`>`标记后面添加一个空格以提高可读性
* 同一段落的连续行可以只在第一行添加`>`标记

### 代码块
    ```lang
    This is a fenced code block.
    ```

    ~~~cpp
    This is another fenced code block.
    ~~~

**注意**：

* `lang`用于指定代码块的代码语言，可选；如果不指定，VNote不会尝试高亮代码
    * 请访问[Prism](https://prismjs.com/#supported-languages)获取一个完整的支持语言列表
* 总是在一个代码块前面添加一个空行是一个不错的实践

### 图表
VNote支持使用以下引擎来绘制图表。您需要使用代码块，并标明特定语言，然后在代码块里面定义图表。

* [Flowchart.js](http://flowchart.js.org/)，语言为`flow`或`flowchart`
* [Mermaid](https://github.com/mermaid-js/mermaid)，语言为`mermaid`
* [WaveDrom](https://wavedrom.com/)，数字时序图，语言为`wavedrom`

例如，

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
VNote支持[PlantUML](http://plantuml.com/)来实现UML图表。您需要使用代码块，并标明语言为`puml`，然后在代码块里面定义图表。

    ```puml
    @startuml
    Bob -> Alice : hello
    @enduml
    ```

#### Graphviz
VNote支持[Graphviz](http://www.graphviz.org/)来绘制图表。您需要使用代码块，并标明语言为`dot`，然后在代码块里面定义图表。

### 数学公式
VNote默认使用[KaTeX](https://katex.org/)渲染数学公式，也可选用[MathJax](https://www.mathjax.org/)。默认的**公式块**的分隔符是`$$...$$`，**行内公式**的分隔符是`$...$`。

* 行内公式不能跨多行
* 形如`3$abc$`/`$abc$4`/`$ abc$`和`$abc $`的不会被解析为公式
* 使用`\`转义`$`
* 对于多行公式，将开始和结束的`$$`分别放在单独的行中；结束分隔符后可以添加公式序号
* 在公式块的`aligned`环境中，使用`\\`来换行

VNote也可以使用标明语言`mathjax`的代码块来实现公式块。这种代码块使用当前选定的数学公式渲染器，也支持KaTeX。

    ```mathjax
    $$
    J(\theta) = \frac 1 2 \sum_{i=1}^m (h_\theta(x^{(i)})-y^{(i)})^2
    $$
    ```

公式块支持公式序号：

    $$vnote x markdown = awesome$$ (1.2.1)

### 行内代码
```md
Here is some `inline code`.
```

如果想输入一个`` ` ``，需要使用两个`` ` ``来括住它，例如``` `` ` `` ```。要输入两个`` ` ``，则需要使用三个`` ` ``。

### 删除线
```md
Here is a ~~text~~ with strikethrough.
```

### 任务列表
```md
* [x] this is a complete item.
* [ ] this is an incomplete item.
```

### 脚注
```md
This is a footnote [^1].

[^1]: Here is the detail of the footnote.
```

### 上标和下标
```md
This is the 1^st^ superscript.

This is the H~2~O subscript.
```

### 标记
```md
Let's mark the ==word==.
```

### 警告
```md
::: alert-info

这是一个信息文本。

:::

::: alert-danger

这是一个危险文本。

:::
```

可用的一些警告形式如下：

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

### 换行和段落
如果需要换行，您应该在当前行末尾添加两个空格，然后换行。VNote提供快捷键`Shift+Enter`来辅助用户输入两个空格并换行。

如果需要一个新的段落，您应该先插入一个空行然后才输入新的段落的文本。

一般来说，您应该在一个块元素（例如代码块、列表和块引用）后面插入一个空行来显式结束该元素。

[^1]: 该指南参考了[Mastering Markdown](https://guides.github.com/features/mastering-markdown/)。
