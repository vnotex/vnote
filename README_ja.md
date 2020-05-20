# VNote
[中文 Chinese](./README_zh.md) | [英語 English](./README.md)

**VNote は、プログラマとMarkdownをよく理解するノート作成アプリケーションです。**

詳細情報は、[**VNoteのホームページ**](https://tamlok.github.io/vnote)に訪問してください。

![VNote](screenshots/vnote.png)

# ダウンロード
## Windows
### Zipアーカイブ
![Windowsビルド・ステータス](https://ci.appveyor.com/api/projects/status/github/tamlok/vnote?svg=true)

- [Githubリリース](https://github.com/tamlok/vnote/releases)
- 開発中の最新ビルド: [![ダウンロード](https://api.bintray.com/packages/tamlok/vnote/vnote/images/download.svg)](https://bintray.com/tamlok/vnote/vnote/_latestVersion)

Windows XPはサポート**されません。**

### Scoop
VNote can be installed from `extras` bucket of Scoop.

```shell
scoop bucket add extras
scoop install vnote
scoop update vnote
```

## Linux
### AppImage
[![Build Status](https://travis-ci.org/tamlok/vnote.svg?branch=master)](https://travis-ci.org/tamlok/vnote)

主要Linuxディストリビューションむけには、AppImageのVNoteスタンドアロン実行ファイルがあります。**Linuxのパッケージングと配布の支援を歓迎します!**

- [Githubリリース](https://github.com/tamlok/vnote/releases)
- 開発中の最新ビルド: [![ダウンロード](https://api.bintray.com/packages/tamlok/vnote/vnote/images/download.svg)](https://bintray.com/tamlok/vnote/vnote/_latestVersion)

### openSUSE
現在、OpenSUSE Tumbleweedの`vnote`は、OBS上の`home:openuse_zh`プロジェクトからインストールすることができます。次のコマンドを直接実行できます。

```shell
sudo zypper ar https://download.opensuse.org/repositories/home:/opensuse_zh/openSUSE_Tumbleweed/ home:opensuse_zh
sudo zypper ref
sudo zypper in vnote
```

その他のアーキテクチャについては、[software.opensuse.org](https://software.opensuse.org)で`vnote`を検索してください。

Qtバージョンの関係で、Leap 42およびそれ以下のバージョンをサポートしていません。AppImageを使ったり、自分でビルドしてください。

### Arch Linux
Arch Linux上のVNoteは、AURから[vnote](https://aur.archlinux.org/packages/vnote-bin/)のインストールができます。

```shell
git clone https://aur.archlinux.org/vnote-bin.git
cd vnote-bin
makepkg -sic
```

また、最新のマスター[vnote git](https://aur.archlinux.org/packages/vnote-git/)を追跡する開発バージョンもあります。

### NixOS
Thank @kuznero for packaging VNote in NixOS. It should be available in `unstable` and `unstable-small` channels.

## MacOS
[![Build Status](https://travis-ci.org/tamlok/vnote.svg?branch=master)](https://travis-ci.org/tamlok/vnote)

- [Githubリリース](https://github.com/tamlok/vnote/releases)
- 開発中の最新ビルド: [![ダウンロード](https://api.bintray.com/packages/tamlok/vnote/vnote/images/download.svg)](https://bintray.com/tamlok/vnote/vnote/_latestVersion)

cask tapを通じ、homebrewをつかってVNoteをインストールすることもできます。

```shell
brew cask install vnote
```

# 説明
**VNote**は、Qtベースの無料のオープンソースのノート作成アプリケーションで、Markdownに焦点を当てています。VNoteは、特にプログラマーにとって快適な編集エクスペリエンスを提供するように設計されています。

VNoteは、Markdownの単純なエディタ**ではありません**。ノート管理を提供することで、VNoteはマークダウンのノート作成をより簡単に、より楽しくすることができます。

Qtを使用し、VNoteは、**Linux**, **Windows**そして、**macOS**で利用できます。

![VNote主インタフェース](screenshots/_vnotemaini_1525154456_1561295841.png)

# サポート
- [Github issues](https://github.com/tamlok/vnote/issues);
- Email: `tamlokveer at gmail.com`;
- [Slack](https://join.slack.com/t/vnote/shared_invite/enQtNDg2MzY0NDg3NzI4LTVhMzBlOTY0YzVhMmQyMTFmZDdhY2M3MDQxYTBjOTA2Y2IxOGRiZjg2NzdhMjkzYmUyY2VkMWJlZTNhMTQyODU);
- [Telegram](https://t.me/vnotex);

# ハイライト
- 強力な**全文検索**;
- **ユニバーサルエントリ**は、入力するだけで何かに到達することができます。
- 直接クリップボードから画像を挿入;
- **編集**と**読み込み**の両方で、フェンシングされたコードブロックの構文のハイライト表示;
- 編集モードでの画像、図、数式の強力な**その場プレビュー**；
- 図の横並びのライブプレビュー;
- 編集モードと読み込みモードの両方でアウトラインを作成;
- 編集モードと読み込みモードの両方でカスタムスタイル;
- **Vim**モードと協力なショートカットキー;
- 無限レベルのフォルダ;
- 複数のタブとウィンドウ分割;
- [Mermaid](http://knsv.github.io/mermaid/), [Flowchart.js](http://flowchart.js.org/), [MathJax](https://www.mathjax.org/), [PlantUML](http://plantuml.com/), and [Graphviz](http://www.graphviz.org/);
- HiDPIのサポート;
- ノートの添付ファイル;
- テーマとダーク・モード;
- HTML、PDF、PDF(All In One)、イメージなど、拡張性と拡張性に優れたエクスポート

# 寄付
VNoteの開発に、さまざまな方法で支援できます。

- VNoteの開発に注目し、改善のためにフィードバックを送信する
- VNoteを友達に広める。人気がひろまることは、開発者にとって大きなエネルギーになります。
- VNoteの開発に参加し、VNoteを完全にするためにPullRequestを送信します。
- VNoteが本当に役に立ち、VNoteを助けたいと思ったら、VNoteへの寄付を本当に感謝します。

**PayPal**: [PayPal.Me/vnotemd](https://www.paypal.me/vnotemd)

**Alipay**: `tamlokveer@gmail.com`

<img src="screenshots/alipay.png" width="256px" height="256px" />

**WeChat**

<img src="screenshots/wechat_pay.png" width="256px" height="256px" />

[VNoteヘ寄付されたユーザ](https://github.com/tamlok/vnote/wiki/Donate-List)に感謝いたします!

# VNoteのわけ
## Markdown編集とノート管理
VNoteは、ノート管理機能のついた協力なMarkdownエディタ、あるいは快適なMarkdownサポートつきのノート作成アプリケーションを目指しています。もしMarkdownのファンで、勉強や仕事、人生のためにMarkdownのノートを書くのなら、VNoteはあなたにとって正しいツールです。

## 快適なマークダウン体験
### Markdownについての洞察
Markdownは、リッチテキストとは異なり、編集と読み取りの間にある**ギャップ**を持って生まれた、シンプルなマークアップ言語です。このギャップを扱うには、3つの方法があります。

1. 一つの極端な例として、エディタの中には、Markdownを**プレーンテキスト**として扱うだけのものもある。ユーザーは、ごちゃごちゃした黒い文字で自分を失うことがあります。メモの情報を追跡するのは難しくなる。
2. ほとんどのMarkdownエディターは、**編集とプレビューを同時に行うために、2つのパネルを使用します**。ユーザーはテキストの編集中に、楽しいタイプの設定とレイアウトを見ることができるので、より簡単になります。しかし、2つのパネルが画面全体を占め、ユーザは目を左右に動かすことになり、大きな混乱を招くことになりかねません。
3. もう一つの極端な点として、編集の直後にMarkdown要素を変換するエディタもあり、Wordのリッチテキスト文書を編集するのと同じように、Markdownを編集することができます。

ほとんどのエディタは、ギャップを処理するための2番目の方法を選んでいるので、Markdownをつかうときは、常にプレビューを考えていることになります。これは、Markdownの誤解かもしれません。シンプルなマーク言語として設計されたMarkdownは、編集時にテキストの情報を記録し、HTMLに変換された後に美しいタイプの設定を提供することを目的としています。

### トレードオフ:VNoteの方法
VNoteは、チューニングされた**構文ハイライト**とその他の機能によって、ギャップを最小限に抑えて、Markdownのためにベストエフォート*WYSIWYG*を提供します。コンテンツを追跡することを助けることにより、テキストを入力した直後にテキストをプレビューしたり変更する必要がなくなります。

# 機能
## ノートブック・ベースのノート管理
VNoteは、**ノートブック**を使用してノートを管理することができます。OneNoteと同様に、ノートブックはシステム上の任意の場所にホストすることができます。ノートブックは1つのアカウントを表すように設計されています。たとえば、ローカル・ファイルシステムで管理されているノートブックと、OwnCloudサーバ上に保存されている別のノートブックをもつことができます。これは、メモが異なるレベルのセキュリティを必要とする場合に、実際に役立ちます。

ノートブックは、ファイルシステム内の自己完結型フォルダ(ノートブックの*Rootフォルダ*と呼ばれます)に対応しています。フォルダを別の場所(または別のコンピュータ)にコピーして、VNoteにインポートすることができます。

ノートブックは、無限レベルのフォルダを持つことができます。VNoteでは、ノートブック内またはノートブック間でフォルダやノートをコピーまたは移動することができます。 

## シンプルなノート管理
すべてのメモは、平文の設定ファイルによって管理され、平文ファイルとして格納されます。ノートは、VNOTEなしでアクセスできます。外部のファイル同期サービスを使用して、ノートを同期させたり、別のマシンにインポートすることができます。

VNoteは、Markdown(拡張子`md`)とリッチテキストノートの両方をサポートしています。

## 構文ハイライト
VNoteでは、Markdownの正確な構文強調表示をサポートしています。ハイライトのスタイルを調整することで、VNoteを使用してドキュメントを簡単に追跡することができます。

また、VNoteは、**Fenceされたコードブロックに対する**構文ハイライトをサポートしています。これは、現在のあらゆるMarkdownエディタよりも**優れています**。

![Syntax Highlight](screenshots/_1513485266_1616037517.png)

## インプレイスプレビュー
VNoteは、編集モードでのイメージ、図、数式の強力な**その場プレビュー**をサポートしています。

![その場プレビュー](screenshots/_inplacepre_1525155248_405615820.png)

## 快適なイメージ体験
画像をMarkdownに貼り付けするだけで、VNoteは他のすべてのものを管理することになります。VNoteは、同じフォルダ内の指定されたフォルダに、ノートとともに画像を格納します。VNoteは、画像を挿入するときに画像プレビューするウィンドウをポップアップ表示させます。また、VNoteは、画像リンクを削除した後に、自動的に画像ファイルを削除します。

## ユニバーサルエントリと全文検索
VNote には、**正規表現**と**ファジー検索**サポートのある、強力な全文検索が組み込まれています。検索は、すべてのノートブック、現在のノートブック、または現在のフォルダ、名前またはコンテンツを対象に実行することができます。

VNoteは、Vimでの`Ctrl+P`のように、**ユニバーサルエントリ**機能をサポートし、シンプルな入力であらゆる機能へ到達できます。

![ユニバーサルエントリ](screenshots/_universale_1522894821_465772669.png)

## 読み込み&編集モードのインタラクティブなアウトライン表示
VNoteでは、編集モードと閲覧モードの両方でユーザフレンドリなアウトラインビューアが提供されています。アウトライン・ビューアは、HTMLのセグメントではなく、応答するアイテム・ツリーです。

## 強力なショートカット
VNoteは、<x/>Vim Mode**、**キャプテンモード**、<x/>、**ナビゲーションモード**など、編集を容易にする強力なショートカットをサポートしています。これにより、マウスを使用せずに作業を行うことができます。

詳細については、ヘルプメニューの[ショートカットヘルプ](src/resources/docs/shortcuts_en.md)を参照してください。

## 柔軟に調整可能
VNoteでは、背景色、フォント、マークダウンなど、ほとんどすべてのものが調整可能です。VNoteは平文ファイルを使ってすべての構成を記録しますので、そのファイルをコピーすれば、別のコンピュータで新しいVNoteを初期化することができます。

# 依存関係
- [Qt 5.9](http://qt-project.org) (L-GPL v3)
- [PEG Markdown Highlight](http://hasseg.org/peg-markdown-highlight/) (MIT License)
- [Hoedown 3.0.7](https://github.com/hoedown/hoedown/) (ISC License)
- [Marked 0.5.1](https://github.com/markedjs/marked) (MIT License)
- [Highlight.js](https://github.com/isagalaev/highlight.js/) (BSD License)
- [Ionicons 2.0.1](https://github.com/driftyco/ionicons/) (MIT License)
- [markdown-it 8.3.1](https://github.com/markdown-it/markdown-it) (MIT License)
- [markdown-it-headinganchor 1.3.0](https://github.com/adam-p/markdown-it-headinganchor) (MIT License)
- [markdown-it-task-lists 1.4.0](https://github.com/revin/markdown-it-task-lists) (ISC License)
- [markdown-it-footnote](https://github.com/markdown-it/markdown-it-footnote) (MIT License)
- [markdown-it-sub](https://github.com/markdown-it/markdown-it-sub) (MIT License)
- [markdown-it-sup](https://github.com/markdown-it/markdown-it-sup) (MIT License)
- [markdown-it-front-matter](https://github.com/craigdmckenna/markdown-it-front-matter) (MIT License)
- [markdown-it-imsize](https://github.com/tatsy/markdown-it-imsize) (Unknown) (Thanks @Kinka for help)
- [markdown-it-emoji](https://github.com/markdown-it/markdown-it-emoji) (MIT License)
- [markdown-it-texmath](https://github.com/goessner/markdown-it-texmath) (MIT License)
- [markdown-it-container 2.0.0](https://github.com/markdown-it/markdown-it-container) (MIT License)
- [mermaid 7.0.0](https://github.com/knsv/mermaid) (MIT License)
- [MathJax](https://www.mathjax.org/) (Apache-2.0)
- [showdown](https://github.com/showdownjs/showdown) (Unknown)
- [flowchart.js](https://github.com/adrai/flowchart.js) (MIT License)
- [PlantUML](http://plantuml.com/) (MIT License)
- [dom-to-image](https://github.com/tsayen/dom-to-image) (MIT License)
- [turndown](https://github.com/domchristie/turndown) (MIT License)

# ライセンス
VNoteは、[MITライセンス](http://opensource.org/licenses/MIT)の下でライセンスされています。
