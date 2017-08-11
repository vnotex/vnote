# Markdown指南
Markdown是一种轻量级的易用的书写语法。本文是Markdown的一个快速指南[^1]。

## 什么是Markdown？
Markdown是一种通过少量简单的标记字符来格式化文本的方法。您可以用纯文本来书写文档，然后在阅读时呈现一个美观的排版。

其实并没有一个标准的Markdown语法，很多编辑器都会添加自己的扩展语法。不同于此，为了兼容性，VNote仅仅支持那些被广泛使用的基本语法。

## 如何上手Markdown？
如果您是刚接触Markdown，那么比较好的一个方法是逐个学习Markdown语法。刚开始，懂得标题和强调语法就能够写出基本的文档；然后，每天可以学习一个新的语法并不断练习。


## 语法指南
下面是VNote支持的Markdown语法的一个概览。

### 标题
```md
# This is an <h1> tag
## This is an <h2> tag
###### This is an <h6> tag
```

**注意**：

- `#`之后需要至少一个空格；
- 一个标题应该占一整行；

### 强调
```md
*This text will be italic*  
_This text will be italic_  

**This text will be bold**  
__This text will be bold__
```

**注意**：

- VNote推荐使用`*`；
- 如果渲染错误，请尝试在第一个`*`之前以及最后一个`*`之后添加一个空格。如果被标记的文本是以全角符号开始或结尾，一般都需要前后添加一个空格；
- VNote提供快捷键`Ctrl+I`和`Ctrl+B`来插入斜体和粗体；

### 列表
#### 无序列表
```md
* Item 1  
只是一段在Item 1下面的文字。需要注意上面一行结尾有两个空格。
* Item 2
    * Item 2a
    * Item 2b
* Item 3

使用一个空行来来结束一个列表。
```

#### 有序列表
```md
1. Item 1
1. Item 2  
注意，列表前面的序号其实是无关紧要的，渲染时Markdown会自动修改该序号。
3. Item 3
    1. Item 3a
    2. Item 3b
4. Item 4
```

### 图片和链接
```md
![Image Alt Text](/url/to/images.png)

[Link Text](/url/of/the/link)
```

### 块引用
```md
As VNote suggests:

> VNote is the best Markdown note-taking application
> ever.  
>
> THere is two spaces after `ever.` above to insert a
> new line.
```

**注意**：

- `>`标记后面需要至少一个空格；
- 多行连续的引用可以只在第一行添加标记；

### 代码块
    ```lang
    This is a fenced code block.
    ```

**注意**：

- `lang`用于指定代码块的代码语言，可选；

### 行内代码
```md
Here is a `inline code`.
```

### 删除线
```md
Here is a ~~text~~ with strikethrough.
```

**注意**：

- VNote提供快捷键`Ctrl+D`来插入带有删除线的文本；

### 任务列表
```md
- [x] this is a complete item.
- [ ] this is an incomplete item.
```

### 上标和下标
```md
This is a text with subscript H~2~o.

This is a text with superscript 29^th^.
```

### 脚注
```md
This is a footnote [^1].

[^1]: Here is the detail of the footnote.
```

### 换行和段落
如果需要换行，您应该在当前行末尾添加两个空格，然后换行。VNote提供快捷键`Shift+Enter`来辅助用户输入两个空格并换行。

如果需要一个新的段落，您应该先插入一个空行然后才输入新的段落的文本。

一般来说，您应该在一个块元素（例如代码块、列表和块引用）后面插入一个空行来显式结束该元素。

[^1]: 该指南参考了 [Mastering Markdown](https://guides.github.com/features/mastering-markdown/).