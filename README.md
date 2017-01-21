# VNote
***
**VNote** is a Vim-inspired note-taking application, designed specially for **Markdown**.

![VNote](screenshots/vnote.png)

## Description
**VNote** is a Qt-based, free and open source note-taking application, focusing on Markdown. VNote is designed to provide comfortable editing expericnce.

Utilizing Qt, VNote could run on **Linux**, **Windows** and MacOS (not tested yet). Android support will be on the road soon.

![VNote](screenshots/vnote_001.png)

## Why VNote
### Markdown Editor & Note-Taking
VNote tries to be a markdown editor with notes management, or a note-taking application with pleasant Markdown support.

Some popular note-taking applications provide Markdown support, such as WizNote, Youdao Note. But most of them provide poor Markdown experience (especially on Linux).

There are always many powerful Markdown editors. But most lack the functionality to manage all your notes. During the design and implementation, VNote references to [CuteMarked](https://github.com/cloose/CuteMarkEd/) a lot.

### Pleasant Markdown Experience
VNote tries to minimize the gap between editing and preview of Markdown. Instead of using two panels to edit and preview simultaneously, VNote utilizes syntax highlighting to help keeping track of the content. VNote also preview images in place when editing.

VNote also learns a lot from Vim to provide convenient shortcuts.

## Features
### Notebook-Based Notes Management
VNote uses **notebook** to hold your notes. Like OneNote, a notebook can host on any location in your system. A notebook is designed to represent one account. For example, you could have one notebook hosted on local file system and another notebook hosted on OwnCloud server.

A notebook could have infinite levels, directories and notes.

### Simple Notes Management
All your notes are managed by a plaintext configuration files and stored as plaintext files. You could access your notes without VNote. You could use external file synchronization services to synchronize your notes and import them on another machine.

### Minimum Gap Between Editing and Preview
VNote tries to provide the best-effort *WYSIWYG* for Markdown by using proper syntax highlighting and other features.

### Pleasant Image Experience
Just paste your image in the Markdown note, VNote will manage all other stuff. VNote stores images in the `images` folder in the same directory with the note. Furthermore, VNote could preview the images while you are editing.

### Ingenious Shortcuts
VNote supports many pleasant shortcuts which facilitate your editing.

- `Ctrl+D` to enter a simple Vim mode temporarily.
- `Ctrl+B`, `Ctrl+I`, `Ctrl+O` to insert bold, italic and inline-coded texts.

### Interactive Outline Viewer for Editing and Preview
VNote provides a user-friendly outline viewer for both editing and preview mode.

## Downloads

## Build & Development
### Clone & Init
```
git clone https://github.com/tamlok/vnote.git vnote.git
cd vnote.git
git submodule update --init
```

### Download Qt & Have Fun
Download [Qt 5.7.0](http://info.qt.io/download-qt-for-application-development) and open `VNote.pro` as a project.

## Dependencies
- [Qt 5.7](http://qt-project.org) (L-GPL v3)
- [PEG Markdown Highlight](http://hasseg.org/peg-markdown-highlight/) (MIT License)
- [Hoedown 3.0.7](https://github.com/hoedown/hoedown/) (ISC License)
- [Marked](https://github.com/chjj/marked) (MIT License)
- [Highlight.js](https://github.com/isagalaev/highlight.js/) (BSD License)
- [Ionicons 2.0.1](https://github.com/driftyco/ionicons/) (MIT License)

## License
VNote is licensed under the [MIT license](http://opensource.org/licenses/MIT).
