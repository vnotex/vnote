# Markdownガイド
これは、軽量で使いやすい構文であるMarkdownのためのクイックガイド[^1]です。

## Markdownとは何ですか?
Mark downは、いくつかの単純なマーカー文字を使ってテキストをスタイル設定する方法です。文書をプレーンテキストで書くことができ、それを美しい表示で読むことができます。

標準的なMarkdown構文は存在せず、多くのエディタは独自の構文を追加することになります。VNoteは、広く使用されている基本構文のみをサポートしています。

## Markdownの使い方。
Markdownを初めて使用する場合は、構文要素をステップごとに学習しておくことをお勧めします。ヘッダーと強調を知ることは、あなたが生き残るのに十分です。毎日ひとつ、別の新しい構文を学び、それを実践することができます。

## 構文ガイド
ここでは、VNoteによってサポートされるMarkdown構文の概要を示します。

### ヘッダー
```md
#これは<h1>タグです。 
##これは<h2>タグです。 
#####これは、<h6>タグです。
```

**注意事項**:

- `#`の後には、少なくとも1つのスペースが必要です;
- ヘッダーは1行全体を占める必要があります。

### 強調
```md
*このテキストは斜体です* 
_このテキストはイタリックになります_

 **このテキストは太字で表示されます**
 __このテキストは太字になります__
```

**注意事項**:

- `*`がVNoteでは推奨されます;
- レンダリングが失敗した場合は、最初の`*`の前と最後の`*`の後に追加スペースを追加してみてください。囲まれたテキストが完全な幅の句読点で始まるか、終了する場合は、スペースが必要です。
- VNoteは、`Ctrl+I`および`Ctrl+B`による斜体と太字のテキスト挿入ショートカットを提供しています。

### 箇条書き
#### 順序なしリスト
```md
* 項目1  
これは、箇条書きの項目の1つ目です。ここで、項目のあとに二つの空白文字を挿入していることに注意してください。
* 項目 2  
    * 項目 2a
    * 項目 2b
* 項目 3

箇条書きの最後には、上記のような空行が必要です。
```

#### 番号ありリスト
```md
1. 項目 1
1. 項目 2  
ここで、シーケンス番号が関係していないことに注意してください。Markdownは、レンダリング時に自動的にシーケンス番号を付番します。
3. 項目 3
    1. 項目 3a
    2. 項目 3b
4. 項目 4
```

### 表
```md
| col 1 | col 2 | col 3 |
| --- | --- | --- |
| cell1 | cell2 | cell3 |
| cell4 | cell5 | cell6 |
```

### 画像とリンク
```md
![Image Alt Text](/url/to/image.png "Optional Text")

![Image Alt Text](/url/to/image.png "Image specified with width and height" =800x600)

![Image Alt Text](/url/to/image.png =800x600)

![Image Alt Text](/url/to/image.png "Image specified with width" =800x)

![Image Alt Text](/url/to/image.png "Image specified with height" =x600)

[Link Text](/url/of/the/link)
```

**注意事項**:

- リファレンスフォーマットでは、イメージリンクの使用は推奨されません。VNoteは、これらの画像のプレビューをおこないません。
- 画像のサイズの指定については、**Markdown-it**レンダリングエンジンでのみ有効です。

### ブロック引用
```md
VNoteは次のことを示唆しています：

> VNoteは、過去最強のMarkdownノート作成アプリです。  
ここで、二つの空白文字が、アプリです。の後に挿入されていることに注意してください。
```

**注意事項**:

- マーカー`>`の後には、少なくとも1つのスペースが必要です;
- 最初の行には、`>`を一つのみ追加できます;

### フェンスコードブロック
    ```lang
    This is a fenced code block.
    ```
    
    ~~~
    This is another fenced code block.
    ~~~

**注意事項**:

- `lang`はコードの言語を指定するオプションです。もし指定されなければ、VNoteはハイライト表示をしません。

### 図

> `Markdown`メニューから、Flowcharts.js か、Mermaidか、WaveDromを有効にする必要があります。

VNoteは、図を描くために次のエンジンをサポートしています。フェンスコードブロックに、特定の言語を指定して、その中に図の定義を記述する必要があります。

- [Flowchart.js](http://flowchart.js.org/) for *flowchart* with language `flow` or `flowchart`;
- [Mermaid](https://mermaidjs.github.io/) with language `mermaid`;
- [WaveDrom](https://wavedrom.com/) for *digital timing diagram* with language `wavedrom`;

たとえば

    ```flowchart
    st=>start: Start:>http://www.google.com[blank]
    e=>end:>http://www.google.com
    op1=>operation: My Operation
    sub1=>subroutine: My Subroutine
    cond=>condition: Yes
    or No?:>http://www.google.com
    io=>inputoutput: catch something...
    
    st->op1->cond
    cond(yes)->io->e
    cond(no)->sub1(right)->op1
    ```

#### UML

> 設定では、PlantUMLを有効にする必要があります。オンラインPlantUMLサーバを使用する場合は、プライバシーの問題に注意してください。ローカルPlantUMLを選択する場合は、Javaランタイム、PlantUML、およびGraphvizを準備する必要があります。

VNoteは、[PlantUML](http://plantuml.com/)をサポートしてUML図を描画します。フェンスコードブロックの言語として、`puml`を指定し、そのなかにダイヤグラム定義を書くようにしてください。

    ```puml
    @startuml
    Bob -> Alice : hello
    @enduml
    ```

#### Graphviz

> 設定でGraphvizを有効にする必要があります。

VNoteは、図を描くために[Graphviz](http://www.graphviz.org/)をサポートしています。フェンスコードブロックの言語として、`dot`を指定し、そのなかにダイヤグラム定義を書くようにしてください。

### 数式

> `Markdown`メニューでMathJaxを有効にし、VNoteを再起動する必要があります。

VNoteは、[MathJax](https://www.mathjax.org/)により数式をサポートしています。既定の数式区切りは、**独立数式表示**は`$$...$$`で、**インライン数式表示**は`$...$`です。

- インライン数式は複数行にまたがることはできません。
- `3$abc$`や`$abc$4`、`$ abc$`、`$abc $`のような文字列は、数式としては扱われません。
- `$`文字をエスケープするには、`\`を用いてください。
- 数式開始の`$$`の前や、数式後の`$$`のあとには、空白文字のみ置くことができます。

VNoteは、`mathjax`が指定された言語のフェンスコードブロックを通じて、独立数式をサポートすることができます。

    ```mathjax
    $$
    J(\theta) = \frac 1 2 \sum_{i=1}^m (h_\theta(x^{(i)})-y^{(i)})^2
    $$
    ```

独立数式表示では、数式番号がサポートされています:

    $$vnote x markdown = awesome$$ (1.2.1)

### インラインコード
```md
これは、`インラインコード`です。
```

`````を一つ入力するには、`````を二つ入力した文字でかこむ必要があります。たとえば````` ` `````となります。ふたつ`````を入力するには、`````が3つ必要です。

### 取り消し線
```md
ここでは~~テキスト~~を取り消し線付きで示しています。
```

**注意事項**:

- VNoteでは、取消線つき文字列を挿入するために、`Ctrl+D`のショートカットを用意しています。

### タスク・リスト
```md
- [x] これは完了したアイテムです。
- [ ] これは未完了のアイテムです。
```

### 脚注
```md
これは脚注[^1]です。

[^1]:これは脚注の詳細です。
```

### 上付き文字と下付き文字

> `Markdown`メニューで、上付き文字または下付き文字を有効にし、`Markdown-it`レンダラーを使用する必要があります。

```md
これは、1^st^の上付き文字です。

これは、H~2~O下付き文字です。
```

### アラート
`Markdown-it`レンダラを使用すると、警告テキストを追加することができます。

```md
::: alert-info

This is an info text.

:::

::: alert-danger

This is a danger text.

:::
```

使用可能なバリアント:

```
alert-primary
alert-secondary
alert-success
alert-info
alert-warning
alert-danger
alert-light
alert-dark
```

### 改行と段落
新しい行を入力する場合は、現在の行に2つのスペースを追加してから、入力を続行します。VNoteは、その支援のために`Shift+Enter`を提供します。

新しい段落を入力する場合は、空行を追加してから、新しい段落の入力を続ける必要があります。

一般的に、ブロック要素(コードブロック、リスト、ブロッククォートなど)の後に空の行を追加して、明示的に終了する必要があります。

[^1]:このガイドは、[Mastering Markdown](https://guides.github.com/features/mastering-markdown/)を参照しています。
