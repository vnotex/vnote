# 外部程序
VNote 支持通过在节点浏览器上下文菜单中的 `打开方式` 来调用 **外部程序** 打开笔记。

用户需要编辑会话配置来添加自定义外部程序。一个例子如下：

```json
{
    "external_programs": [
        {
            "name" : "gvim",
            "command" : "C:\\\"Program Files (x86)\"\\Vim\\vim80\\gvim.exe %1",
            "shortcut" : "F4"
        },
        {
            "name" : "notepad",
            "command" : "notepad %1",
            "shortcut" : ""
        }
    ]
}
```

一个外部程序可以包含3个属性：

1. `name`: 该程序在 VNote 中的名字；
2. `command`: 当使用该外部程序打开笔记时执行的命令；
    1. 使用 `%1` 占位符，会被替换为真实的文件路径(自动加上双引号包裹)；
3. `shortcut`: 分配给该外部程序的快捷键；

修改配置前请关闭 VNote。
