# Template
VNote supports creating a note from a template.

When creating a note, you could choose one template in the dialog.

![](vx_images/5480642170752.png)

VNote stores template files in the `templates` folder. One file corresponds to one template.

You could add or delete template files directly in the template folder via system's file browser.

VNote supports **Snippet** in template. For example, you could write a template like this:

```md
# %no%
This is a template using **Snippet** to insert note name as the title automatically.
```

`%no%` is a built-in snippet which will be evaluated to the note name (without suffix). Hence if the note name is `week report.md`, then the new note will look like this:

```md
# week report
This is a template using **Snippet** to insert note name as the title automatically.
```