# 幻词
**幻词**是一些具有特殊含义的字符。它们将被识别为一些预先定义好的字符。举一个简单的例子，`date`可以识别为今天的日期。

VNote在大多数输入窗口小部件中支持片段插入。例如，您可以在创建备注时插入片段作为备注名称。

`%da% work log.md`将识别为`20180128 work log.md` ，因为`%da%`是一个幻词，它以`YYYYMMDD`的形式定义为今天的日期。

在编辑器中，您可以键入 `%da%`，然后按快捷键 `Ctrl+E M`，它将光标下的单词识别为幻词。

例如，键入以下单词：

```
Today is %da%
```

然后按 `Ctrl+E M`，它将更改为：

```
Today is 20180128
```

## 内置幻词
VNote定义了许多幻词。在输入对话框中，键入`%help%` 以显示已定义的幻词列表。

![](_v_images/_1517138965_254456675.png)

## 自定义幻词
在配置文件夹中编辑 `vnote.ini` 文件，如下所示：

```ini
[magic_words]
1\name=vnote
1\definition="vnote is a great tool! -- Written %datetime%"
2\name=hw
2\definition="hello world!"
size=2
```

现在我们得到了两个幻词分别是：`vnote`（基于另一个幻词`datetime`）和`hw`。

## 在片段中的幻词
[Snippet](snippet.html) 也支持幻词。现在我们可以定义一个片段来插入当前日期，如下所示：

![](_v_images/_1517139520_1176992512.png)

现在处于编辑模式，我们可以按`Ctrl+E S D`来插入当前日期。