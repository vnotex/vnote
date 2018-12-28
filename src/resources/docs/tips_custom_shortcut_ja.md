# ショートカットのカスタマイズ
VNoteでは、標準ショートカットのカスタマイズもサポートされていますが、推奨されません。VNoteは、ユーザー構成ファイル`vnote.ini`の`[shortcuts]`セクションおよび`[captain_mode_shortcuts]`セクションに外部プログラムの構成情報を格納します。

たとえば、デフォルトの設定は次のようになります。

```ini
[shortcuts]
; Define shortcuts here, with each item in the form "operation=keysequence".
; Leave keysequence empty to disable the shortcut of an operation.
; Customized shortcuts may conflict with some key bindings in edit mode or Vim mode.
; Ctrl+Q is reserved for quitting VNote.

; Leader key of Captain mode
CaptainMode=Ctrl+E
; Create a note in current folder
NewNote=Ctrl+Alt+N
; Save current note
SaveNote=Ctrl+S
; Save changes and enter read mode
SaveAndRead=Ctrl+T
; Edit current note
EditNote=Ctrl+W
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
ShortcutsHelp=?
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

各項目は、`操作=キーシーケンス`の形式です。操作のショートカットを無効にするには、`キーシーケンス`を空にします。

`Ctrl+Q`は、Vnoteを終了するために予約されていることに注意してください。
