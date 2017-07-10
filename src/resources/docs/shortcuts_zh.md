# VNote快捷键说明
1. 以下按键除特别说明外，都不区分大小写；
2. 在macOS下，`Ctrl`对应于`Command`,在Vim模式下除外。

## 常规快捷键
- `Ctrl+E E`  
是否扩展编辑区域。
- `Ctrl+N`  
在当前文件夹下新建笔记。
- `Ctrl+F`  
页内查找和替换。
- `Ctrl+Q`  
退出VNote。
- `Ctrl+J`/`Ctrl+K`  
在笔记本列表、文件夹列表、笔记列表、已打开笔记列表和大纲目录中，均支持`Ctrl+J`和`Ctrl+K`导航。
- `Ctrl+Left Mouse`  
任意滚动。

### 阅读模式
- `Ctrl+W`  
编辑当前笔记。
- `H`/`J`/`K`/`L`  
导航，对应于左/下/上/右方向键。
- `Ctrl+U`  
向上滚动半屏。
- `Ctrl+D`  
向下滚动半屏。
- `gg`/`G`  
跳转到笔记的开始或结尾。（区分大小写）。
- `Ctrl + +/-`    
放大/缩小页面。
- `Ctrl+Wheel`    
鼠标滚轮实现放大/缩小页面。
- `Ctrl+0`  
恢复页面大小为100%。

### 编辑模式
- `Ctrl+S`  
保存当前更改。
- `Ctrl+T`  
保存当前更改并退出编辑模式。

#### 文本编辑
- `Ctrl+B`  
插入粗体；再次按`Ctrl+B`退出。如果已经选择文本，则将当前选择文本加粗。
- `Ctrl+I`  
插入斜体；再次按`Ctrl+I`退出。如果已经选择文本，则将当前选择文本改为斜体。
- `Ctrl+D`  
插入删除线；再次按`Ctrl+D`退出。如果已经选择文本，则将当前选择文本改为删除线。
- `Ctrl+O`  
插入行内代码；再次按`Ctrl+O`退出。如果已经选择文本，则将当前选择文本改为行内代码。
- `Ctrl+H`  
退格键，向前删除一个字符。
- `Ctrl+W`  
删除光标位置向后到第一个空白字符之间的所有字符。
- `Ctrl+U`  
删除光标位置到行首的所有字符。
- `Ctrl+<Num>`  
插入级别为`<Num>`的标题。`<Num>`应该是1到6的一个数字。如果已经选择文本，则将当前选择文本改为标题。
- `Tab`/`Shift+Tab`  
增加或减小缩进。如果已经选择文本，则对所有选择的行进行缩进操作。
- `Shift+Enter`  
插入两个空格然后换行，在Markdown中类似于软换行的概念。
- `Shift+Left`, `Shift+Right`, `Shift+Up`, `Shift+Down`  
扩展选定左右一个字符，或上下一行。
- `Ctrl+Shift+Left`, `Ctrl+Shift+Right`  
扩展选定到单词开始或结尾。
- `Ctrl+Shift+Up`, `Ctrl+Sfhit+Down`  
扩展选定到段尾或段首。
- `Shift+Home`, `Shift+End`  
扩展选定到行首和行尾。
- `Ctrl+Shift+Home`, `Ctrl+Shift+End`  
扩展选定到笔记开始或结尾处。

## 自定义快捷键
VNote支持自定义部分标准快捷键（但并不建议这么做）。VNote将快捷键信息保存在用户配置文件`vnote.ini`中的`[shortcuts]`小节。

例如，默认的配置可能是这样子的：


```ini
[shortcuts]
1\operation=NewNote
1\keysequence=Ctrl+N
2\operation=SaveNote
2\keysequence=Ctrl+S
3\operation=SaveAndRead
3\keysequence=Ctrl+T
4\operation=EditNote
4\keysequence=Ctrl+W
5\operation=CloseNote
5\keysequence=
6\operation=Find
6\keysequence=Ctrl+F
7\operation=FindNext
7\keysequence=F3
8\operation=FindPrevious
8\keysequence=Shift+F3
size=8
```

`size=8` 告诉VNote这里定义了8组快捷键，每组快捷键都以一个数字序号开始。通过改变每组快捷键中`keysequence`的值来改变某个操作的默认快捷键。将`keysequence`设置为空（`keysequence=`）则会禁用该操作的任何快捷键。

注意，`Ctrl+E`保留作为*舰长模式*的前导键，`Ctrl+Q`保留为退出VNote。

# 舰长模式
为了更有效地利用快捷键，VNote支持 **舰长模式**。

按前导键`Ctrl+E`后，VNote会进入舰长模式。在舰长模式中，VNote会支持更多高效的快捷操作。

另外，在该模式中，`Ctrl+W`和`W`是等效的，因此，可以`Ctrl+E+W`来实现`Ctrl+E W`的操作。

- `E`   
是否扩展编辑区域。
- `P`   
切换单列/双列面板模式。
- `T`   
打开或关闭工具面板。
- `F`   
打开当前分割窗口的笔记列表。在该列表中，可以直接按笔记对应的序号实现跳转。
- `X`   
关闭当前标签页。
- `J`   
跳转到下一个标签页。
- `K`   
跳转到上一个标签页。
- `1` - `9`  
数字1到9会跳转到对应序号的标签页。
- `0`   
跳转到前一个标签页（即前一个当前标签页）。实现当前标签页和前一个标签页之间的轮换。
- `D`   
定位当前笔记所在目录。
- `Q`   
放弃当前更改并退出编辑模式。
- `V`   
竖直分割当前窗口。
- `R`   
移除当前分割窗口。
- `H`   
跳转到左边一个分割窗口。
- `L`   
跳转到右边一个分割窗口。
- `Shift+H`   
将当前标签页左移一个分割窗口。
- `Shift+L`  
将当前标签页右移一个分割窗口。
- `?`   
显示本快捷键说明。

## 展览模式
在舰长模式中，`W`命令会进入 **展览模式**。在展览模式中，VNote会在常用的主要部件上显示至多两个字母，此时输入对应的字母即可跳转到该部件中，从而实现快速切换焦点并触发功能。

# Vim Mode
VNote支持一个简单但有用的Vim模式，包括 **正常**， **插入**， **可视**， **可视行** 模式。

VNote支持以下几个Vim的特性：

- `r`, `s`, `i`, `I`, `a`, `A`, `o`, `O`;
- 操作 `d`, `c`, `y`, `p`, `<`, `>`, `gu`, `gU`, `~`；
- 移动 `h/j/k/l`, `gj/gk`, `Ctrl+U`, `Ctrl+D`, `gg`, `G`, `0`, `^`, `$`；
- 标记 `a-z`；
- 寄存器 `"`, `_`, `+`, `a-z`(`A-Z`)；
- 跳转位置列表 (`Ctrl+O` and `Ctrl+I`)；
- 前导键 (`Space`)
    - 目前 `<leader>y/d/p` 等同于 `"+y/d/p`, 从而可以访问系统剪切板；
- `zz`, `zb`, `zt`;
- `u` 和 `Ctrl+R` 撤销和重做；
- 文本对象 `i/a`：word, WORD, `''`, `""`, `` ` ` ``, `()`, `[]`, `<>`, and `{}`;
- 命令行 `:w`, `:wq`, `:x`, `:q`, and `:q!`;
- 标题跳转
    - `[[`：跳转到上一个标题；
    - `]]`: 跳转到下一个标题；
    - `[]`：跳转到上一个同层级的标题；
    - `][`：跳转到下一个同层级的标题；
    - `[{`：跳转到上一个高一层级的标题；
    - `]}`：跳转到下一个高一层级的标题；

VNote目前暂时不支持Vim的宏和重复(`.`)特性。

在VNote上享受Vim的美好时光吧！
