# VNote ショートカット
1. 特別な説明がないすべてのキーは**大文字と小文字が区別されません**;
2. mac OSでは、`Ctrl`はVimモードを除いて`Command`に対応しています。

## 通常のショートカット
- `Ctrl+E E`
 編集領域の拡張を切り替えます。
- `Ctrl+Alt+N`
現在のフォルダにノートを作成します。
- `Ctrl+F`
現在のノートで検索/置換します。
- `Ctrl+Alt+F`
 詳細検索の実行。
- `Ctrl+Q`
VNoteを終了します。
- `Ctrl+J`/`Ctrl+K`
 Vnoteは、ノートブック・リスト、ディレクトリー・リスト、ノート・リスト、オープン・ノート・リスト、アウトライン・リストでのナビゲーション用に`Ctrl+J`と`Ctrl+K`をサポートしています。
- `Ctrl+左マウス`
すべての方向にスクロールします。
- `Ctrl+Shift+T]`
最後に閉じたファイルをリカバリします。
- `Ctrl+Alt+L`
 Flashページを開きます。
- `Ctrl+Alt+I`
クイックアクセスを開きます。
- `Ctrl+T`
現在のノートの編集を開始するか、変更を保存して編集を終了します。
- `Ctrl+G`
Universal Entryをアクティブにします。
- `Ctrl+8`/`Ctrl+9`
最後の検索アクションで、次の/前の一致にジャンプします。

### 読み取りモード
- `H`/`J``K``L`
移動、それぞれ左/下/上/右キー。
- `Ctrl+U`
半画面上スクロール
- `Ctrl+D`
半画面下スクロール
- `gg`/`G`ノートの先頭または末尾へ移動。(大文字小文字を識別).
- `Ctrl+ +`
ズームイン/ズームアウト。
- `Ctrl+wheel`マウスのスクロールにあわせてズームイン/ズームアウト。
- `Ctrl+O`
ページのズームレベルを100%にリセット。
- タイトル間ジャンプ
   - `<N>[[`: 前の `N`タイトルにジャンプする。
   - `<N>[[`: 次の `N`タイトルにジャンプする。
   - `<N>[[`: 同じレベルの前の `N`タイトルにジャンプする。
   - `<N>[[`: 同じレベルの次の `N`タイトルにジャンプする。
   - `<N>[[`: 上のレベルの前の `N`タイトルにジャンプする。
   - `<N>[[`: 上のレベルの次の `N`タイトルにジャンプする。
- `/` または `?` 前方検索または後方検索
   - `N`:次を検索
   - `Shift+N`: 前を検索
- `:` Vimコマンドモード
   - `:q`: 現在のノートを閉じる。
   - `:noh[lsearch]`: 検索ハイライトをクリアー。

### 編集モード
- `Ctrl+S`
現在の変更を保存する。
- `Ctrl+ +`
ズームイン/ズームアウト。
- `Ctrl+wheel`マウスのスクロールにあわせてズームイン/ズームアウト。
- `Ctrl+O`
ページのズームレベルを100%にリセット。
- `Ctrl+J/K`
カーソル位置を変更せずに、ページダウン/アップ スクロール。
- `Ctrl+N/P`
自動補完を有効化。
   - `Ctrl+N/P`
自動補完リスト上を移動し、選択した補完候補を挿入する。
   - `Ctrl+J/K`
自動補完リスト上を移動する。
   - `Ctrl+E`
自動補完をキャンセル。
   - `Enter` 
現在の補完を挿入します。
   - `Ctrl+[`]または`Escape` 
補完を完了。

#### テキスト編集
- `Ctrl+B` 
太字を挿入します。`Ctrl+B`を再度押して終了します。現在選択されているテキストは太字に変更されます。
- `Ctrl+I` 
斜体を挿入します。`Ctrl+I`を再度押して、終了します。現在選択されているテキストはイタリックに変更されます。
- `Ctrl+D` 
取消線を挿入します。`Ctrl+D`をもう一度押して終了します。現在選択されているテキストは取り消し線に変更されます。
- `Ctrl+;` 
インライン・コードを挿入します。`Ctrl+;`をもう一度押して終了します。現在選択されているテキストは、存在する場合はインライン・コードに変更されます。
- `Ctrl+M` 
フェンス付きコードブロックを挿入します。`Ctrl+M`を再度押して、終了します。現在選択されているテキストは、存在する場合はコードブロックにラップされます。
- `Ctrl+L` リンクを挿入します。
- `Ctrl+.` テーブルを挿入します。
- `Ctrl+'` 画像を挿入します。
- `Ctrl+H` バックスペース。前の文字を削除します。
- `Ctrl+W` 
現在のカーソルから1番目最初のスペースまでのすべての文字を削除します。
- `Ctrl+U` 
現在のカーソルから現在の行の先頭の間のすべての文字を削除します。
- `Ctrl+<数値>`
レベル`<数値>`のタイトル行を挿入します。`<数値>`は1から6である必要があります。現在選択されているテキストはタイトルに変更されます。
- `Ctrl+7`
 現在の行または選択したテキストのタイトル記号を削除します。
- `Tab`/`Shift+Tab`
Increase or decrease the indentation. テキストが選択されている場合、インデントはこれらの選択されたすべての行に対して動作します。
- `Shift+入力` 2つのスペースを挿入し、その後に新しい行を挿入します。つまり、Markdownにソフトラインブレークを挿入します。
- `Shift+左`、`Shift+右`、`Shift+上`、`Shift+下` 選択範囲を1文字左または右に拡張するか、1行上または下に拡大します。
- `Ctrl+Shift+左`、`Ctrl+Shift+右` 
選択範囲を現在の単語の先頭または末尾に拡大します。
- `Ctrl+Shift+上`、`Ctrl+Sfhit+下` 
選択範囲を現在の段落の先頭または末尾に拡大します。
- `Shift+Home`、`Shift+End` 
選択範囲を現在の線分の先頭または末尾に拡大します。
- `Ctrl+Shift+Home`、`Ctrl+Shift+End` 
選択範囲を現在の注記の先頭または末尾に拡大します。

## ショートカットのカスタマイズ
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

各項目は、`操作=キーシーケンス`の形式です。操作のショートカットを無効にするには、`キーシーケンス`を空にします。

`Ctrl+Q`は、Vnoteを終了するために予約されていることに注意してください。

# キャプテンモード
ショートカットを効率的に利用するために、VNoteは**キャプテンモード**をサポートしています。

リーダーキー`Ctrl+E`を押すと、VNoteがキャプテンモードに入り、VNoteがより効率的なショートカットをサポートするようになります。

- `E`
 編集領域の拡張を切り替えます。
- `Y` 
編集領域にフォーカスします。
- `T` 
ツールパネルを切り替えます。
- `Shift+#` 
ツールバーを切り替えます。
- `F` 
現在の分割ウィンドウの開いているノートリストをポップアップします。このリストの中で、各ノートの前でシーケンス番号を押すと、そのノートにジャンプできます。
- `A` 
現在のノートの添付ファイルリストをポップアップします。
- `X` 
現在のタブを閉じます。
- `J` 
次のタブにジャンプします。
- `K` 
前のタブにジャンプします。
- `1`-`9`
 番号キー1～9は、対応するシーケンス番号を持つタブにジャンプします。
- `0` 
前のタブにジャンプします。現在のタブと前のタブとの間で切り替えます。
- `D` 
現在のノートのフォルダに移動します。
- `Q` 
現在の変更を破棄し、編集モードを終了します。
- `V` 
カレントウィンドウ垂直に分割します。
- `R` 
現在の分割ウィンドウを削除します。
- `Shift+]`
 現在の分割ウィンドウを最大化します。
- `=` 
すべての分割ウィンドウを均等に分配します。
- `H` 
左側の最初の分割ウィンドウにジャンプします。
- `L` 
右側の最初の分割ウィンドウにジャンプします。
- `Shift+H` 
現在のタブを一つ左の分割ウィンドウに移動します。
- `Shift+L` 
現在のタブを1つの分割ウィンドウ右側に移動します。
- `M` 
現在のカーソルワードまたは選択されたテキストをマジックワードとして評価します。
- `S`
 編集モードでスニペットを適用します。
- `O` 
ノートをエクスポートします。
- `I` 
ライブプレビューパネルを切り替えます。
- `U` 
ライブプレビューパネルを展開します。
- `C` 
全文検索を切り替えます。
- `P`
クリップボードのHTMLをMarksownテキストとして貼り付けます。
- `N`  
View and edit current note's information.
- `Shift+?` 
ショートカットのドキュメントを表示します。

## ナビゲーション・モード
キャプテンモードでは、`W`はVNoteを**ナビゲーションモード**に変える。このモードでは、VNoteは主要ウィジェットに最大2文字の文字を表示し、その後対応する文字を押すとそのウィジェットにジャンプします。

# Vimモード
VNoteは、**Normal**、**Insert**、**Visual**、および**VisualLine**の各モードを含む、単純で有用なVimモードをサポートしています。

::: alert-info

`ファイル`メニューから、`設定`ダイアログを開き、 `読み取り/編集` タブへすすみます。 コンボボックスの`キーモード`を
選択することでVimモードを有効にできます。 すぐに稼動させるには、VNoteの再起動が必要です。

:::

VNoteはVimの次の機能をサポートしています：

- `r`, `s`, `S`, `i`, `I`, `a`, `A`, `c`, `C`, `o`, and `O`;
- Actions `d`, `c`, `y`, `p`, `<`, `>`, `gu`, `gU`, `J`, `gJ`, and `~`;
- Movements `h/j/k/l`, `gj/gk/g0`, `Ctrl+U`, `Ctrl+D`, `gg`, `G`, `0`, `^`, `{`, `}`, and `$`;
- マーク`a-z`;
- Registers `"`, `_`, `+`, `a-z`(`A-Z`);
- ジャンプ位置リスト(`Ctrl+O`および`Ctrl+I`);
- リーダーキー (`Space`)
   - 現在のところ、`<leader>y/d/p`は、`"+y/d/p`と同等で、システムのクリップボードにアクセスします;
   - `<leader><Space>`検索ハイライトをクリアします。
   - `<leader>w`ノートを保存します。
- `zz`, `zb`, `zt`;
- 取り消しとやり直しには`u`と`Ctrl+R`を指定します。
- Text objects `i/a`: word, WORD, `''`, `""`, `` ` ` ``, `()`, `[]`, `<>`, and `{}`;
- コマンドライン `:w`および, `:wq`, `:x`, `:q`, `:q!`, `:noh[lsearch]`;
- タイトル間ジャンプ
   - `[[`: 前のタイトルにジャンプ;
   - `]]`: 次のタイトルにジャンプ。
   - `[]`: 同じレベルで前のタイトルにジャンプします。
   - `][`: 同じレベルで次のタイトルにジャンプします。
   - `[{`: 上位レベルの前のタイトルにジャンプする;
   - `[{`: 上位レベルの次のタイトルにジャンプする;
- 検索するには`/`と`?`
   - `n`と`N`により、次の出現または前の出現を見つける;
   - `Ctrl+N`と`Ctrl+P`は、検索履歴をナビゲートするために使用します;
- `Ctrl+R`レジスタの内容を読み出す;
- 挿入モード時に`Ctrl+O`で、通常モードに一時的に入る;

現在のところ、VNoteは、Vimのマクロと再実行(`.`)機能をサポート**しません**。

VNoteでVimを楽しんでください!

# その他
- `Ctrl+J`と`Ctrl+K`は、アイテム間を移動するために使用できます;
- `Ctrl+N`と`Ctrl+P`は、リスト内の検索結果中を移動するために使用できます;
