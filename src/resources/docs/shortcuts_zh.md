# VNote快捷键说明
1. 以下按键除特别说明外，都不区分大小写；
2. 在macOS下，`Ctrl`对应于`Command`,在Vim模式下除外。

## 常规快捷键
- `Ctrl+E E`  
是否扩展编辑区域。
- `Ctrl+Alt+N`  
在当前文件夹下新建笔记。
- `Ctrl+F`  
页内查找和替换。
- `Ctrl+Alt+F`  
高级查找。
- `Ctrl+Q`  
退出VNote。
- `Ctrl+J`/`Ctrl+K`  
在笔记本列表、文件夹列表、笔记列表、已打开笔记列表和大纲目录中，均支持`Ctrl+J`和`Ctrl+K`导航。
- `Ctrl+Left Mouse`  
任意滚动。
- `Ctrl+Shift+T`  
恢复上一个关闭的文件。
- `Ctrl+Alt+L`  
打开灵犀页。
- `Ctrl+Alt+I`  
打开快速访问。
- `Ctrl+T`  
编辑当前笔记或保存更改并退出编辑模式。
- `Ctrl+G`  
激活通用入口。
- `Ctrl+8`/`Ctrl+9`  
跳转到最近一次查找的下一个/上一个匹配。

### 阅读模式
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
- 标题跳转
    - `<N>[[`：跳转到上`N`个标题；
    - `<N>]]`: 跳转到下`N`个标题；
    - `<N>[]`：跳转到上`N`个同层级的标题；
    - `<N>][`：跳转到下`N`个同层级的标题；
    - `<N>[{`：跳转到上`N`个高一层级的标题；
    - `<N>]}`：跳转到下`N`个高一层级的标题；
- `/`或`?`向前或向后查找
    - `N`：查找下一个匹配；
    - `Shift+N`：查找上一个匹配；
- `:`执行Vim命令
    - `:q`：关闭当前笔记；
    - `:noh[lsearch]`：清空查找高亮；

### 编辑模式
- `Ctrl+S`  
保存当前更改。
- `Ctrl + +/-`  
放大/缩小页面。
- `Ctrl+Wheel`  
鼠标滚轮实现放大/缩小页面。
- `Ctrl+0`  
恢复页面大小为100%。
- `Ctrl+J/K`  
向下/向上滚动页面，不会改变光标。
- `Ctrl+N/P`  
激活自动补全。
    - `Ctrl+N/P`  
    浏览补全列表并插入当前补全。
    - `Ctrl+J/K`  
    浏览补全列表。
    - `Ctrl+E`  
    取消补全。
    - `Enter`  
    插入补全。
    - `Ctrl+[` or `Escape`  
    结束补全。

#### 文本编辑
- `Ctrl+B`  
插入粗体；再次按`Ctrl+B`退出。如果已经选择文本，则将当前选择文本加粗。
- `Ctrl+I`  
插入斜体；再次按`Ctrl+I`退出。如果已经选择文本，则将当前选择文本改为斜体。
- `Ctrl+D`  
插入删除线；再次按`Ctrl+D`退出。如果已经选择文本，则将当前选择文本改为删除线。
- `Ctrl+;`  
插入行内代码；再次按`Ctrl+;`退出。如果已经选择文本，则将当前选择文本改为行内代码。
- `Ctrl+M`  
插入代码块；再次按`Ctrl+M`退出。如果已经选择文本，则将当前选择文本嵌入到代码块中。
- `Ctrl+L`  
插入链接。
- `Ctrl+.`  
插入表格。
- `Ctrl+'`  
插入图片。
- `Ctrl+H`  
退格键，向前删除一个字符。
- `Ctrl+W`  
删除光标位置向后到第一个空白字符之间的所有字符。
- `Ctrl+U`  
删除光标位置到行首的所有字符。
- `Ctrl+<Num>`  
插入级别为`<Num>`的标题。`<Num>`应该是1到6的一个数字。如果已经选择文本，则将当前选择文本改为标题。
- `Ctrl+7`  
删除当前行或所选择文本的标题标记。
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
VNote支持自定义部分标准快捷键（但并不建议这么做）。VNote将快捷键信息保存在用户配置文件`vnote.ini`中的`[shortcuts]`和`[captain_mode_shortcuts]`两个小节。

例如，默认的配置可能是这样子的：


```ini
[shortcuts]
; Define shortcuts here, with each item in the form "operation=keysequence".
; Leave keysequence empty to disable the shortcut of an operation.
; Custom shortcuts may conflict with some key bindings in edit mode or Vim mode.
; Ctrl+Q is reserved for quitting VNote.

; Leader key of Captain mode
CaptainMode=Ctrl+E
; Create a note in current folder
NewNote=Ctrl+Alt+N
; Save current note
SaveNote=Ctrl+S
; Close current note
CloseNote=
; Open file/replace dialog
Find=Ctrl+F
; Find next occurence
FindNext=F3
; Find previous occurence
FindPrevious=Shift+F3

[captain_mode_shortcuts]
; Define shortcuts in Captain mode here.
; There shortcuts are the sub-sequence after the CaptainMode key sequence
; in [shortcuts].

; Enter Navigation mode
NavigationMode=W
; Show attachment list of current note
AttachmentList=A
; Locate to the folder of current note
LocateCurrentFile=D
; Toggle Expand mode
ExpandMode=E
; Alternate one/two panels view
OnePanelView=P
; Discard changes and enter read mode
DiscardAndRead=Q
; Toggle Tools dock widget
ToolsDock=T
; Close current note
CloseNote=X
; Show shortcuts help document
ShortcutsHelp=Shift+?
; Flush the log file
FlushLogFile=";"
; Show opened files list
OpenedFileList=F
; Activate the ith tab
ActivateTab1=1
ActivateTab2=2
ActivateTab3=3
ActivateTab4=4
ActivateTab5=5
ActivateTab6=6
ActivateTab7=7
ActivateTab8=8
ActivateTab9=9
; Alternate between current and last tab
AlternateTab=0
; Activate next tab
ActivateNextTab=J
; Activate previous tab
ActivatePreviousTab=K
; Activate the window split on the left
ActivateSplitLeft=H
; Activate the window split on the right
ActivateSplitRight=L
; Move current tab one split left
MoveTabSplitLeft=Shift+H
; Move current tab one split right
MoveTabSplitRight=Shift+L
; Create a vertical split
VerticalSplit=V
; Remove current split
RemoveSplit=R
```

每一项配置的形式为`操作=按键序列`。如果`按键序列`为空，则表示禁用该操作的快捷键。

注意，`Ctrl+Q`保留为退出VNote。

# 舰长模式
为了更有效地利用快捷键，VNote支持 **舰长模式**。

按前导键`Ctrl+E`后，VNote会进入舰长模式。在舰长模式中，VNote会支持更多高效的快捷操作。

- `E`   
是否扩展编辑区域。
- `Y`   
将焦点设为编辑区域。
- `T`   
打开或关闭工具面板。
- `Shift+#`   
打开或关闭工具栏。
- `F`   
打开当前分割窗口的笔记列表。在该列表中，可以直接按笔记对应的序号实现跳转。
- `A`   
打开当前笔记的附件列表。
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
定位当前笔记所在文件夹。
- `Q`   
放弃当前更改并退出编辑模式。
- `V`   
垂直分割当前窗口。
- `R`   
移除当前分割窗口。
- `Shift+|`   
最大化当前分割窗口。
- `=`   
均等分布所有分割窗口。
- `H`   
跳转到左边一个分割窗口。
- `L`   
跳转到右边一个分割窗口。
- `Shift+H`   
将当前标签页左移一个分割窗口。
- `Shift+L`  
将当前标签页右移一个分割窗口。
- `M`  
编辑模式中，将当前光标所在词或者所选文本进行幻词解析。
- `S`  
在编辑模式中应用片段。
- `O`  
导出笔记。
- `I`  
打开或关闭实时预览面板。
- `U`  
扩展或还原实时预览面板。
- `C`  
打开或关闭全文查找。
- `P`  
解析剪切板中的HTML为Markdown文本并粘贴。
- `N`  
查看和编辑当前笔记信息。
- `Shift+?`   
显示本快捷键说明。

## 展览模式
在舰长模式中，`W`命令会进入 **展览模式**。在展览模式中，VNote会在常用的主要部件上显示至多两个字母，此时输入对应的字母即可跳转到该部件中，从而实现快速切换焦点并触发功能。

# Vim Mode
VNote支持一个简单但有用的Vim模式，包括 **正常**， **插入**， **可视**， **可视行** 模式。

::: alert-info

在`文件`菜单中选择`设置`打开对话框，跳转到`阅读/编辑`标签页，在`按键模式`下拉框中选择开启Vim即可。需要重启VNote以生效。

:::

VNote支持以下几个Vim的特性：

- `r`, `s`, `S`, `i`, `I`, `a`, `A`, `c`, `C`, `o`, `O`;
- 操作 `d`, `c`, `y`, `p`, `<`, `>`, `gu`, `gU`, `J`, `gJ`, `~`；
- 移动 `h/j/k/l`, `gj/gk/g0`, `Ctrl+U`, `Ctrl+D`, `gg`, `G`, `0`, `^`, `{`, `}`, `$`；
- 标记 `a-z`；
- 寄存器 `"`, `_`, `+`, `a-z`(`A-Z`)；
- 跳转位置列表 (`Ctrl+O` and `Ctrl+I`)；
- 前导键 (`Space`)
    - 目前 `<leader>y/d/p` 等同于 `"+y/d/p`, 从而可以访问系统剪切板；
    - `<leader><Space>` 清除查找高亮；
    - `<leader>w` 保存笔记；
- `zz`, `zb`, `zt`;
- `u` 和 `Ctrl+R` 撤销和重做；
- 文本对象 `i/a`：word, WORD, `''`, `""`, `` ` ` ``, `()`, `[]`, `<>`, `{}`;
- 命令行 `:w`, `:wq`, `:x`, `:q`, `:q!`, `:noh[lsearch]`;
- 标题跳转
    - `[[`：跳转到上一个标题；
    - `]]`: 跳转到下一个标题；
    - `[]`：跳转到上一个同层级的标题；
    - `][`：跳转到下一个同层级的标题；
    - `[{`：跳转到上一个高一层级的标题；
    - `]}`：跳转到下一个高一层级的标题；
- `/` 和 `?` 开始查找
    - `n` 和 `N` 查找下一处或上一处；
    - `Ctrl+N` 和 `Ctrl+P` 浏览查找历史；
- `Ctrl+R` 读取指定寄存器的值；
- `Ctrl+O` 在插入模式中临时切换为正常模式；
- `/`

VNote目前暂时不支持Vim的宏和重复(`.`)特性。

在VNote上享受Vim的美好时光吧！

# 其他
- `Ctrl+J` 和 `Ctrl+K` 浏览导航；
- 在列表中，`Ctrl+N` 和 `Ctrl+P` 在搜索结果中导航；
