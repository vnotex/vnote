# 外部程序
VNote支持使用外部程序来打开笔记。VNote将外部程序的配置信息保存在用户配置文件`vnote.ini`的`[external_editors]`小节中。

一个配置可能如下：

```ini
[external_editors]
; Define external editors which could be called to edit notes
; One program per line with the format name="program %0 arg1 arg2"
; in which %0 will be replaced with the note file path
; Need to escape \ and ", use double quotes to quote paths/arguments with spaces

GVim=C:\\\"Program Files (x86)\"\\Vim\\vim80\\gvim.exe %0
Notepad=notepad %0
Notepad%2B%2B=C:\\\"Program Files (x86)\"\\notepad++\\notepad++.exe %0
```

VNote需要重启以检测新的外部程序。
