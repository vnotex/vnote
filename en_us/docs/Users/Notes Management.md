# Notes Management
VNote adopts **notebooks-folders-notes** hierarchy for notes management. A notebook corresponds to a directory in the file system, which is called **Notebook Root Folder**. Folders of a notebook correspond to directories within the Notebook Root Folder. Notes inside a folder corresponds to files within that directory.

## Notebook
Notebook is an independent, self-explanatory container in VNote. A notebook is a **Notebook Root Folder** in the file system. The root folder contains all the notes and configuration files of that notebook.

### Create A Notebook
You could create a new notebook by specifying following fields:

- **Notebook Name**
    - Name of your notebook in VNote. It is only used to identify your notebook in VNote. It will not be written into the configuration of the notebook.
- **Notebook Root Folder**
    - Choose an **EMPTY** directory in your system to hold all the contents of this notebook. This choosen directory is assumed to be in the control of VNote.
- **Image Folder**
    - This is the name of the folder used to store local images of notes. VNote uses a given folder which has the same parent folder of the notes to store images of those notes.
- **Attachment Folder**
    - This is the name of the folder used to store attachment files of notes.

### Migrate and Import A Notebook
A notebook is an independent directory in the file system, so you could just copy or synchronize the *Notebook Root Folder* to migrate a notebook.

You could import an existing notebook into VNote by selecting its *Notebook Root Folder* when creating a notebook. VNote will try to read the configuration files to restore the notebook.

Combining these, you could create your notebooks in a directory which is synchronized via third-party service, such as Dropbox and OneDrive, and then in another computer, you could import that directory into VNote as a notebook. With this, you could use VNote to edit and manage your notes, which will be synchronized by other trusted services, both at home and at work.

## Folders
The hierarchy of folders within a notebook is the same as that of the directories within the *Notebook Root Folder*. You could create as many as possible levels of folders theoritically.

## Notes
You could import external files into VNote as notes by the `New Notes From Files` action in `File` menu. VNote will **copy** selected files into current folder as notes.

In theory,  a note in VNote could use any suffix. Notes with suffix `.md` will be treated as Markdown files.