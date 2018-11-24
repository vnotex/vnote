# 模板
VNote支持基于模板创建笔记。

创建笔记时，您可以在对话框中选择一个模板。

![](_v_images/_1517711911_1371452209.png)

VNote将模板文件存储在配置文件夹的templates子文件夹中。一个文件对应一个模板。您可以通过单击组合框旁边的「文件夹」图标来访问模板文件夹。![](_v_images/_1517712063_1523124418.png) 

您可以通过系统的文件浏览器在模板文件夹中添加或删除模板文件。

VNote支持模板中的**Magic Word**。例如，您可以编写如下模板：

```md
# %no%
This is a template using **Magic Word** to insert note name as the title automatically.
```

`%no%` 是一个Magic Word，将笔记的名称自动识别为标题（不带后缀）。 因此，如果笔记命名为 `week report.md`，则新笔记将如下所示：

```md
# week report
This is a template using **Magic Word** to insert note name as the title automatically.
```