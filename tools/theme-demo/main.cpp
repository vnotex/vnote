#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmapCache>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QSvgRenderer>
#include <QTabBar>
#include <QTabWidget>
#include <QTest>
#include <QTextBlock>
#include <QTextStream>
#include <QTimer>
#include <QToolBar>
#include <QToolBox>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineView>
#include <QWindow>

#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>

#include <core/theme.h>
#include <vtextedit/markdowneditorconfig.h>
#include <vtextedit/markdownhighlighter.h>
#include <vtextedit/texteditorconfig.h>
#include <vtextedit/theme.h>
#include <vtextedit/vmarkdowneditor.h>
#include <vtextedit/vtextedit.h>
#include <vtextedit/vtexteditor.h>

namespace {

const QStringList &themeNames() {
  static const QStringList names = {
      QStringLiteral("everforest-dark"), QStringLiteral("latex-dark"),
      QStringLiteral("latex-light"),     QStringLiteral("moonlight"),
      QStringLiteral("solarized-dark"),  QStringLiteral("solarized-light"),
      QStringLiteral("vscode-dark"),     QStringLiteral("vue-dark"),
      QStringLiteral("vue-light"),       QStringLiteral("vx-idea")};
  return names;
}

const QString sourceRoot = QString::fromUtf8(VNOTE_SOURCE_DIR);
const QString webRoot = sourceRoot + QStringLiteral("/src/data/extra/web");
constexpr int readinessTimeout = 15000;

void require(bool condition, const QString &message) {
  if (!condition) {
    throw std::runtime_error(message.toUtf8().constData());
  }
}

QString readRequired(const QString &path) {
  QFile file(path);
  require(file.open(QIODevice::ReadOnly),
          QStringLiteral("Cannot read %1: %2").arg(path, file.errorString()));
  const auto bytes = file.readAll();
  require(file.error() == QFileDevice::NoError && !bytes.trimmed().isEmpty(),
          QStringLiteral("Empty or unreadable resource: %1").arg(path));
  return QString::fromUtf8(bytes);
}

QJsonObject parseObject(const QString &text, const QString &path) {
  QJsonParseError error;
  const auto doc = QJsonDocument::fromJson(text.toUtf8(), &error);
  require(error.error == QJsonParseError::NoError && doc.isObject() && !doc.object().isEmpty(),
          QStringLiteral("Invalid JSON object in %1: %2").arg(path, error.errorString()));
  return doc.object();
}

void requireResolved(const QString &text, const QString &name) {
  static const QRegularExpression refs(QStringLiteral("@(palette|base|widgets)#"));
  require(!text.trimmed().isEmpty() && !refs.match(text).hasMatch(),
          QStringLiteral("Empty or unresolved theme resource: %1").arg(name));
}

// Validate reference types and cycles before the production resolver's assertions.
QString paletteLeaf(const QJsonObject &palette, const QString &path, QSet<QString> &visiting) {
  require(!visiting.contains(path), QStringLiteral("Cyclic palette reference: %1").arg(path));
  visiting.insert(path);
  QJsonValue value(palette);
  for (const auto &part : path.split(QLatin1Char('#'))) {
    value = value.toObject().value(part);
  }
  require(value.isString() && !value.toString().isEmpty(),
          QStringLiteral("Missing palette string: %1").arg(path));
  auto result = value.toString();
  if (result.startsWith(QLatin1Char('@'))) {
    result = paletteLeaf(palette, result.mid(1), visiting);
  }
  visiting.remove(path);
  return result;
}

void validatePalette(const QJsonObject &root, const QJsonObject &object, const QString &prefix) {
  require(!object.isEmpty(), QStringLiteral("Missing palette section: %1").arg(prefix));
  for (auto it = object.begin(); it != object.end(); ++it) {
    const auto path = prefix + QLatin1Char('#') + it.key();
    if (it->isObject()) {
      validatePalette(root, it->toObject(), path);
    } else {
      QSet<QString> visiting;
      paletteLeaf(root, path, visiting);
    }
  }
}

void validateSyntaxTheme(const QString &name) {
  require(!name.isEmpty(), QStringLiteral("Empty syntax theme name"));
  if (QFileInfo(name).isAbsolute()) {
    const auto object = parseObject(readRequired(name), name);
    require(!object.value(QStringLiteral("text-styles")).toObject().isEmpty(),
            QStringLiteral("Missing syntax text-styles: %1").arg(name));
    requireResolved(QString::fromUtf8(QJsonDocument(object).toJson()), name);
    return;
  }
  const QDir dir(sourceRoot + QStringLiteral("/src/data/extra/syntax-highlighting/themes"));
  for (const auto &file : dir.entryList({QStringLiteral("*.theme")}, QDir::Files)) {
    const auto path = dir.filePath(file);
    const auto object = parseObject(readRequired(path), path);
    if (object.value(QStringLiteral("metadata"))
            .toObject()
            .value(QStringLiteral("name"))
            .toString() == name) {
      require(!object.value(QStringLiteral("text-styles")).toObject().isEmpty(),
              QStringLiteral("Missing syntax text-styles: %1").arg(path));
      return;
    }
  }
  require(false, QStringLiteral("Unknown source syntax theme: %1").arg(name));
}

QString localUrl(const QString &path) {
  return QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
}

QString stylesheetLink(const QString &path) {
  readRequired(path);
  return QStringLiteral("<link rel=\"stylesheet\" href=\"%1\">\n")
      .arg(localUrl(path).toHtmlEscaped());
}

QString localizeCssUrls(QString css, const QString &directory) {
  const QRegularExpression pattern(QStringLiteral("url\\(\\s*(['\"]?)([^)'\"]+)\\1\\s*\\)"));
  int offset = 0;
  while (true) {
    const auto match = pattern.match(css, offset);
    if (!match.hasMatch()) {
      return css;
    }
    const auto url = match.captured(2).trimmed();
    if (QUrl(url).isRelative() && !url.startsWith(QLatin1Char('#'))) {
      const auto path = QDir(directory).absoluteFilePath(url);
      readRequired(path);
      const auto replacement = QStringLiteral("url(\"%1\")").arg(localUrl(path));
      css.replace(match.capturedStart(), match.capturedLength(), replacement);
      offset = match.capturedStart() + replacement.size();
    } else {
      offset = match.capturedEnd();
    }
  }
}

struct Assets {
  QString name;
  QString folder;
  QString qss;
  QString editor;
  QString web;
  std::unique_ptr<vnotex::Theme> theme;
};

Assets loadAssets(const QString &name) {
  require(themeNames().contains(name), QStringLiteral("Unknown or excluded theme: %1").arg(name));
  Assets assets;
  assets.name = name;
  assets.folder = sourceRoot + QStringLiteral("/src/data/extra/themes/") + name;
  const auto palette =
      parseObject(readRequired(assets.folder + QStringLiteral("/palette.json")), name);
  for (const auto &section :
       {QStringLiteral("palette"), QStringLiteral("base"), QStringLiteral("widgets")}) {
    validatePalette(palette, palette.value(section).toObject(), section);
  }
  const auto editor =
      parseObject(readRequired(assets.folder + QStringLiteral("/text-editor.theme")), name);
  require(editor.value(QStringLiteral("metadata"))
                      .toObject()
                      .value(QStringLiteral("type"))
                      .toString() == QStringLiteral("vtextedit") &&
              !editor.value(QStringLiteral("editor-styles")).toObject().isEmpty() &&
              !editor.value(QStringLiteral("markdown-syntax-styles")).toObject().isEmpty(),
          QStringLiteral("Invalid VTextEdit theme: %1").arg(name));
  for (const auto &file : {QStringLiteral("interface.qss"), QStringLiteral("web.css"),
                           QStringLiteral("highlight.css")}) {
    readRequired(assets.folder + QLatin1Char('/') + file);
  }
  for (const auto &file : {QStringLiteral("editor-highlight.theme"),
                           QStringLiteral("markdown-editor-highlight.theme")}) {
    const auto path = assets.folder + QLatin1Char('/') + file;
    if (QFileInfo::exists(path)) {
      validateSyntaxTheme(path);
    }
  }
  assets.theme.reset(vnotex::Theme::fromFolder(assets.folder));
  require(bool(assets.theme), QStringLiteral("Cannot resolve theme: %1").arg(name));
  assets.qss = assets.theme->fetchQtStyleSheet();
  assets.editor = assets.theme->fetchTextEditorStyle();
  assets.web = assets.theme->fetchWebStyleSheet();
  requireResolved(assets.qss, name + QStringLiteral(" QSS"));
  requireResolved(assets.editor, name + QStringLiteral(" editor"));
  requireResolved(assets.web, name + QStringLiteral(" CSS"));
  parseObject(assets.editor, name + QStringLiteral(" resolved editor"));
  validateSyntaxTheme(assets.theme->getEditorHighlightTheme());
  validateSyntaxTheme(assets.theme->getMarkdownEditorHighlightTheme());
  assets.web = localizeCssUrls(assets.web, assets.folder);
  // All image roles, including disabled neighbors, must be genuine source SVGs.
  const QDir folder(assets.folder);
  const auto svgs = folder.entryList({QStringLiteral("*.svg")}, QDir::Files);
  require(!svgs.isEmpty(), QStringLiteral("Missing SVG resources: %1").arg(name));
  for (const auto &svg : svgs) {
    QSvgRenderer renderer(folder.filePath(svg));
    require(renderer.isValid(), QStringLiteral("Invalid SVG: %1").arg(folder.filePath(svg)));
  }
  return assets;
}

void paintTurn() {
  QCoreApplication::processEvents(QEventLoop::AllEvents);
  QTest::qWait(80);
}

void moveMouse(QWidget *widget, const QPoint &position) {
  // The QWidget overload relies on native cursor warping, which remote Windows
  // sessions may ignore. Send the QtTest event through the exposed QWindow.
  auto *window = widget->window()->windowHandle();
  require(window != nullptr, QStringLiteral("Mouse target has no exposed window"));
  QTest::mouseMove(window, window->mapFromGlobal(widget->mapToGlobal(position)));
}

void waitUntil(const std::function<bool()> &ready, const QString &description) {
  QElapsedTimer timer;
  timer.start();
  while (!ready()) {
    require(timer.elapsed() < readinessTimeout,
            QStringLiteral("Timed out waiting for %1").arg(description));
    QTest::qWait(20);
  }
  paintTurn();
}

QVariant javascript(QWebEnginePage *page, const QString &code) {
  struct Result {
    bool finished = false;
    QVariant value;
  };
  // A timed-out callback can still arrive; never capture a stack reference.
  const auto result = std::make_shared<Result>();
  page->runJavaScript(code, [result](const QVariant &value) {
    result->value = value;
    result->finished = true;
  });
  waitUntil([result]() { return result->finished; },
            QStringLiteral("WebEngine JavaScript callback"));
  return result->value;
}

const char cppSample[] = R"CPP(// Comments, names, constants, search needle and folding
#include <string>
#include <vector>
#define DEMO_LIMIT 42
namespace sample {
struct Record {
    std::string name = "needle";
    bool enabled = true;
};
int count(const std::vector<Record> &records) {
    int total = 0;
    for (const auto &record : records) {
        if (record.enabled && record.name == "needle") {
            ++total; // needle: current search match
        }
    }
    return total + DEMO_LIMIT;
}
} // namespace sample
// needle: another occurrence
)CPP";

const char markdownSample[] = R"MD(---
title: Theme visibility needle
---
<!-- A comment: enabled text, not disabled content. -->
# Heading one needle
## Heading two
### Heading three
#### Heading four
##### Heading five
###### Heading six
Ordinary **strong**, *emphasis*, ~~strike~~ and ==mark==.
[Readable link](https://example.invalid/a/very/long/destination/that/is/concealed)
- List item needle
  - Nested list item
1. Ordered list
> Quote with `inline code` and a needle.

```cpp
// Fenced comment needle
const int answer = 42;
std::string value = "needle";
```

| Name | Value |
| :--- | ---: |
| needle | 42 |

Inline math $x^2 + y^2$ and display source:
$$
E = mc^2
$$

    Verbatim needle
[reference]: https://example.invalid/another/long/concealed/destination
)MD";

QString readerFixture() {
  QString html = QStringLiteral(R"HTML(
<section id="reader-top">
<h1>Heading one <a class="vx-header-anchor" href="#reader-top" vx-data-anchor-icon="#"></a></h1>
<h2>Heading two</h2><h3>Heading three</h3><h4>Heading four</h4><h5>Heading five</h5><h6>Heading six</h6>
<p id="reader-prose">Ordinary prose with <strong>strong text</strong>, <em>emphasis</em>, <del>strikethrough</del>,
<a id="reader-link" href="#reader-code">an underlined link</a>, <a id="visited-link" href="#visited-target">a visited same-document link</a>,
<code>inline needle</code>, <mark>marked text</mark>, <span class="vx-search-match">search needle</span> and
<span class="vx-current-search-match">current needle</span>.</p>
<div id="visited-target">Local anchor destination; no network navigation.</div>
<blockquote>Direct quote text <p>Nested quote paragraph with <a href="#reader-code">a link</a> and <code>code</code>.</p></blockquote>
<ul><li>Unordered item<ul><li>Nested item</li></ul></li></ul><ol><li>Ordered item</li></ol>
<table id="reader-table"><thead><tr><th>Table heading</th><th>Value</th></tr></thead><tbody>
<tr><td>Odd stripe text</td><td><a href="#reader-code">link</a></td></tr>
<tr id="table-hover"><td>Even stripe and hover text</td><td><code>needle</code></td></tr>
<tr><td>Another stripe</td><td>42</td></tr></tbody></table>
</section>
<section id="reader-alerts"><h2>Alerts and short aliases</h2><div style="display:grid;grid-template-columns:1fr 1fr;gap:12px;padding-left:12px">
)HTML");
  const QStringList alerts = {QStringLiteral("alert-primary"), QStringLiteral("alert-secondary"),
                              QStringLiteral("alert-success"), QStringLiteral("alert-s"),
                              QStringLiteral("alert-warning"), QStringLiteral("alert-w"),
                              QStringLiteral("alert-info"),    QStringLiteral("alert-i"),
                              QStringLiteral("alert-danger"),  QStringLiteral("alert-d"),
                              QStringLiteral("alert-light"),   QStringLiteral("alert-dark")};
  for (const auto &variant : alerts) {
    html +=
        QStringLiteral("<div class=\"vx-alert %1\" style=\"position:relative\">%1: ordinary text "
                       "with <a href=\"#reader-code\">link</a> and <code>code</code>.</div>")
            .arg(variant);
  }
  html += QStringLiteral(R"HTML(</div></section>
<section id="reader-graphs"><h2>Styled graph containers (not graph engines)</h2>
<div class="vx-mermaid-graph">Mermaid container caption</div><div class="vx-plantuml-graph">PlantUML container caption</div>
<div class="vx-flowchartjs-graph">Flowchart container caption</div><div class="vx-wavedrom-graph">WaveDrom container caption</div>
</section>
<section id="reader-code"><h2>Real local Prism syntax and toolbar controls</h2>
<pre class="line-numbers"><code class="language-cpp">// Comment needle
#include &lt;string&gt;
namespace sample { const int answer = 42; }
std::string value = "needle";</code></pre>
<pre class="line-numbers"><code class="language-javascript">// A comment and an inserted token
const needle = { enabled: true, amount: 42 };
function value(input) { return input.amount + 1; }</code></pre>
<pre class="line-numbers"><code class="language-json">{"needle": "value", "enabled": true, "count": 42, "empty": null}</code></pre>
<pre class="line-numbers"><code class="language-markdown"># Heading
A **strong** word, `code` and [link](#reader-top).</code></pre>
<pre><code>Untyped code: needle &lt;sample&gt; &amp; ordinary text.</code></pre>
<pre><code class="language-diff">+ inserted needle
- deleted needle</code></pre>
</section>
)HTML");
  return html;
}

QString readerHtml(const Assets &assets, int highlightMode, bool tips) {
  auto html = readRequired(webRoot + QStringLiteral("/markdown-viewer-template.html"));
  html.replace(QStringLiteral("<head>"), QStringLiteral(R"JS(<head><script>
window.__themeDemoReady = false;
window.__themeDemoError = '';
window.addEventListener('error', function(event) {
    window.__themeDemoError = event.message || ('Resource failed: ' + (event.target.src || event.target.href || 'unknown'));
}, true);
</script>)JS"));
  const auto globals = stylesheetLink(webRoot + QStringLiteral("/css/user.css")) +
                       stylesheetLink(webRoot + QStringLiteral("/css/globalstyles.css"));
  const QRegularExpression globalPlaceholder(
      QStringLiteral("<style[^>]*>\\s*/\\* VX_GLOBAL_STYLES_PLACEHOLDER \\*/\\s*</style>"));
  require(globalPlaceholder.match(html).hasMatch(),
          QStringLiteral("Missing global stylesheet placeholder"));
  html.replace(globalPlaceholder, globals);
  QString highlightFile = assets.folder + QStringLiteral("/highlight.css");
  if (assets.name == QStringLiteral("vx-idea")) {
    // Validate every optional source even before an optional mode is selected.
    readRequired(assets.folder + QStringLiteral("/code_highlight/highlight-one-light.css"));
    readRequired(assets.folder + QStringLiteral("/code_highlight/highlight-dark.css"));
    readRequired(assets.folder + QStringLiteral("/tips_components/sytle.css"));
    if (highlightMode == 1) {
      highlightFile = assets.folder + QStringLiteral("/code_highlight/highlight-one-light.css");
    } else if (highlightMode == 2) {
      highlightFile = assets.folder + QStringLiteral("/code_highlight/highlight-dark.css");
    }
  }
  requireResolved(readRequired(highlightFile), highlightFile);
  const auto themeStyles =
      QStringLiteral("<style>\n%1\n</style>\n").arg(assets.web) + stylesheetLink(highlightFile);
  html.replace(QStringLiteral("<!-- VX_THEME_STYLES_PLACEHOLDER -->"), themeStyles);
  auto resources = stylesheetLink(webRoot + QStringLiteral("/css/imageviewer.css")) +
                   stylesheetLink(webRoot + QStringLiteral("/css/markdownit.css")) +
                   stylesheetLink(webRoot + QStringLiteral("/css/codeblockactions.css"));
  if (tips && assets.name == QStringLiteral("vx-idea")) {
    resources += stylesheetLink(assets.folder + QStringLiteral("/tips_components/sytle.css"));
  }
  html.replace(QStringLiteral("<!-- VX_STYLES_PLACEHOLDER -->"), resources);
  html.replace(QStringLiteral("/* VX_GLOBAL_OPTIONS_PLACEHOLDER */"), QString());
  const auto prismPath = webRoot + QStringLiteral("/js/prism/prism.min.js");
  readRequired(prismPath);
  const auto scripts = QStringLiteral("<script data-manual src=\"%1\"></script>\n")
                           .arg(localUrl(prismPath).toHtmlEscaped()) +
                       QStringLiteral(R"JS(<script>
window.addEventListener('load', function() {
    try {
        if (!window.Prism) throw new Error('Local Prism did not load');
        var content = document.getElementById('vx-content');
        Prism.highlightAllUnder(content, false);
        ['cpp', 'javascript', 'json', 'markdown'].forEach(function(language) {
            if (!content.querySelector('code.language-' + language + ' .token')) throw new Error('Missing Prism tokens: ' + language);
        });
        // The viewer normally creates these containers. This fixture uses the
        // production DOM shape even when the bundled Prism has no toolbar plugin.
        content.querySelectorAll('pre').forEach(function(pre) {
            if (!pre.parentElement.classList.contains('code-toolbar')) {
                var wrapper = document.createElement('div'); wrapper.className = 'code-toolbar';
                pre.parentNode.insertBefore(wrapper, pre); wrapper.appendChild(pre);
            }
        });
        content.querySelectorAll('div.code-toolbar').forEach(function(container) {
            var toolbar = container.querySelector('.toolbar');
            if (!toolbar) { toolbar = document.createElement('div'); toolbar.className = 'toolbar'; container.appendChild(toolbar); }
            var label = document.createElement('div'); label.className = 'toolbar-item';
            label.innerHTML = '<span>Code</span>'; toolbar.appendChild(label);
            ['copy', 'collapse'].forEach(function(kind) {
                var item = document.createElement('div'); item.className = 'toolbar-item';
                var button = document.createElement('button'); button.className = 'vx-codeblock-action-btn';
                button.type = 'button'; button.title = kind;
                button.innerHTML = kind === 'copy'
                    ? '<svg viewBox="0 0 24 24" aria-label="Copy"><rect x="8" y="8" width="12" height="12" rx="2"/><path d="M16 8V4H4v12h4"/></svg>'
                    : '<svg viewBox="0 0 24 24" aria-label="Collapse"><path d="m6 14 6-6 6 6"/></svg>';
                item.appendChild(button); toolbar.appendChild(item);
            });
        });
        var localDocument = location.href.split('#')[0];
        document.querySelectorAll('a[href^="#"]').forEach(function(link) {
            link.href = localDocument + link.getAttribute('href');
        });
        document.getElementById('visited-link').click();
        window.scrollTo(0, 0);
        document.fonts.ready.then(function() {
            document.querySelectorAll('link[rel="stylesheet"]').forEach(function(link) {
                if (!link.sheet) throw new Error('Stylesheet not loaded: ' + link.href);
            });
            document.fonts.forEach(function(font) {
                if (font.status === 'error') throw new Error('Local font failed: ' + font.family);
            });
            requestAnimationFrame(function() { requestAnimationFrame(function() {
                window.__themeDemoReady = true;
            }); });
        }).catch(function(error) { window.__themeDemoError = String(error); });
    } catch (error) { window.__themeDemoError = String(error); }
});
</script>)JS");
  html.replace(QStringLiteral("<!-- VX_SCRIPTS_PLACEHOLDER -->"), scripts);
  require(html.contains(QStringLiteral("<div id=\"vx-content\"></div>")),
          QStringLiteral("Missing production reader content container"));
  html.replace(QStringLiteral("<div id=\"vx-content\"></div>"),
               QStringLiteral("<div id=\"vx-content\">%1</div>").arg(readerFixture()));
  return html;
}

class DemoView : public QWidget {
public:
  explicit DemoView(Assets &&loaded, int highlightMode, bool tips, QWidget *parent = nullptr)
      : QWidget(parent), assets(std::move(loaded)) {
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    tabs = new QTabWidget(this);
    layout->addWidget(tabs);
    tabs->addTab(makeControls(), QStringLiteral("Controls"));
    tabs->addTab(makeEditors(), QStringLiteral("Editors"));
    tabs->addTab(makeReader(highlightMode, tips), QStringLiteral("Reader"));
  }

  ~DemoView() override {
    // The profile must outlive its pages, irrespective of QObject child order.
    delete web;
  }

  Assets assets;
  QTabWidget *tabs = nullptr;
  QToolButton *hoverButton = nullptr;
  QToolButton *actionButton = nullptr;
  QToolButton *dangerButton = nullptr;
  QPushButton *pushButton = nullptr;
  QLineEdit *lineEdit = nullptr;
  QTreeWidget *tree = nullptr;
  QListWidget *list = nullptr;
  QMenu *menu = nullptr;
  QToolButton *menuButton = nullptr;
  vte::VTextEditor *textEditor = nullptr;
  vte::VMarkdownEditor *markdownEditor = nullptr;
  QWebEngineView *web = nullptr;
  QWebEngineProfile *profile = nullptr;
  QComboBox *highlightSelector = nullptr;
  QCheckBox *tipsToggle = nullptr;
  bool markdownReady = false;
  bool webFinished = false;
  bool webSucceeded = false;

  void waitForEditors() {
    tabs->setCurrentIndex(1);
    markdownReady = false;
    markdownEditor->getHighlighter()->updateHighlight();
    waitUntil([this]() { return markdownReady; }, QStringLiteral("Markdown highlightCompleted"));
    for (auto editor : {textEditor, static_cast<vte::VTextEditor *>(markdownEditor)}) {
      editor->peekText(QStringLiteral("needle"), vte::FindFlags());
      const auto found = editor->findText({QStringLiteral("needle")}, vte::FindFlags());
      require(found.m_totalMatches > 1,
              QStringLiteral("Editor search fixture was not highlighted"));
      QTextCursor cursor(editor->document());
      cursor.movePosition(QTextCursor::Start);
      editor->getTextEdit()->setTextCursor(cursor);
      editor->scrollToLine(0, false);
    }
    paintTurn();
  }

  void waitForReader() {
    tabs->setCurrentIndex(2);
    waitUntil([this]() { return webFinished; }, QStringLiteral("WebEngine loadFinished"));
    require(webSucceeded, QStringLiteral("WebEngine failed to load reader fixture"));
    QElapsedTimer timer;
    timer.start();
    while (true) {
      const auto state =
          javascript(
              web->page(),
              QStringLiteral(
                  "({ready:window.__themeDemoReady === true,error:window.__themeDemoError || ''})"))
              .toMap();
      require(state.value(QStringLiteral("error")).toString().isEmpty(),
              QStringLiteral("Reader resource failure: %1")
                  .arg(state.value(QStringLiteral("error")).toString()));
      if (state.value(QStringLiteral("ready")).toBool()) {
        break;
      }
      require(timer.elapsed() < readinessTimeout,
              QStringLiteral("Timed out waiting for Prism, fonts and reader paint"));
      QTest::qWait(20);
    }
    require(javascript(web->page(), QStringLiteral(R"JS((function() {
            var content = document.getElementById('vx-content');
            var rect = content.getBoundingClientRect();
            return rect.width > 100 && rect.height > 100 && content.innerText.includes('Ordinary prose')
                && content.querySelectorAll('code .token').length > 10;
        })())JS"))
                .toBool(),
            QStringLiteral("Reader fixture has no painted content geometry"));
    paintTurn();
  }

private:
  QWidget *semanticSurface(const QString &role) {
    auto panel = new QFrame;
    panel->setObjectName(QStringLiteral("roleSurface"));
    // These are the exact production planes, not a replacement QPalette.
    panel->setStyleSheet(QStringLiteral("QFrame#roleSurface { background-color: %1; }")
                             .arg(assets.theme->paletteColor(QStringLiteral("base#") + role +
                                                             QStringLiteral("#bg"))));
    auto layout = new QGridLayout(panel);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->addWidget(new QLabel(role + QStringLiteral(" surface")), 0, 0);
    auto muted = new QLabel(QStringLiteral("Muted enabled text"));
    muted->setProperty("MutedText", true);
    layout->addWidget(muted, 0, 1);
    int column = 2;
    for (const auto &severity :
         {QStringLiteral("info"), QStringLiteral("warning"), QStringLiteral("error")}) {
      auto label = new QLabel(severity + QStringLiteral(" text"));
      label->setProperty("SeverityText", severity);
      layout->addWidget(label, 0, column++);
    }
    column = 0;
    for (const auto &state : {QStringLiteral("success"), QStringLiteral("warning"),
                              QStringLiteral("error"), QStringLiteral("info")}) {
      auto label = new QLabel(state + QStringLiteral(" state"));
      label->setProperty("State", state);
      label->setMargin(3);
      layout->addWidget(label, 1, column++);
    }
    return panel;
  }

  QWidget *makeControls() {
    auto page = new QWidget(this);
    auto layout = new QVBoxLayout(page);
    layout->setSpacing(5);
    layout->setContentsMargins(6, 6, 6, 6);
    auto toolbar = new QToolBar(QStringLiteral("Real toolbar"), page);
    auto tool = [toolbar](const QString &text) {
      auto button = new QToolButton;
      button->setText(text);
      button->setFocusPolicy(Qt::StrongFocus);
      toolbar->addWidget(button);
      return button;
    };
    hoverButton = tool(QStringLiteral("Checked tool / hover / press"));
    hoverButton->setCheckable(true);
    hoverButton->setChecked(true);
    actionButton = tool(QStringLiteral("Action focus"));
    actionButton->setProperty("ActionToolButton", true);
    dangerButton = tool(QStringLiteral("Danger focus"));
    dangerButton->setProperty("DangerButton", true);
    auto disabledTool = tool(QStringLiteral("Disabled text tool"));
    disabledTool->setEnabled(false);
    menuButton = tool(QStringLiteral("Menu"));
    menu = new QMenu(menuButton);
    menu->addAction(QStringLiteral("Normal action"));
    auto checked = menu->addAction(QStringLiteral("Checked action"));
    checked->setCheckable(true);
    checked->setChecked(true);
    auto group = new QActionGroup(menu);
    auto radio = menu->addAction(QStringLiteral("Radio action"));
    radio->setCheckable(true);
    radio->setChecked(true);
    group->addAction(radio);
    menu->addAction(QStringLiteral("Disabled action"))->setEnabled(false);
    menu->addMenu(QStringLiteral("Submenu"))->addAction(QStringLiteral("Nested action"));
    menuButton->setMenu(menu);
    menuButton->setPopupMode(QToolButton::InstantPopup);
    layout->addWidget(toolbar);

    auto buttons = new QHBoxLayout;
    pushButton = new QPushButton(QStringLiteral("Checked push button"));
    pushButton->setCheckable(true);
    pushButton->setChecked(true);
    buttons->addWidget(pushButton);
    auto standard = new QPushButton(QStringLiteral("Default / primary"));
    standard->setDefault(true);
    buttons->addWidget(standard);
    auto master = new QLabel(QStringLiteral("Master role: expanded toolbox title"));
    master->setMargin(8);
    master->setStyleSheet(QStringLiteral("color: %1; background-color: %2;")
                              .arg(assets.theme->paletteColor(QStringLiteral("base#master#fg")),
                                   assets.theme->paletteColor(QStringLiteral("base#master#bg"))));
    buttons->addWidget(master);
    auto disabledPush = new QPushButton(QStringLiteral("Disabled push"));
    disabledPush->setEnabled(false);
    buttons->addWidget(disabledPush);
    layout->addLayout(buttons);

    auto fields = new QHBoxLayout;
    lineEdit = new QLineEdit(QStringLiteral("Selected input needle"));
    fields->addWidget(lineEdit);
    auto empty = new QLineEdit;
    empty->setPlaceholderText(QStringLiteral("Empty input / placeholder"));
    fields->addWidget(empty);
    auto combo = new QComboBox;
    combo->addItems({QStringLiteral("Combo value"), QStringLiteral("Second value")});
    fields->addWidget(combo);
    auto disabledCombo = new QComboBox;
    disabledCombo->addItem(QStringLiteral("Disabled combo"));
    disabledCombo->setEnabled(false);
    fields->addWidget(disabledCombo);
    auto spin = new QSpinBox;
    spin->setRange(0, 100);
    spin->setValue(42);
    fields->addWidget(spin);
    auto disabledSpin = new QSpinBox;
    disabledSpin->setValue(12);
    disabledSpin->setEnabled(false);
    fields->addWidget(disabledSpin);
    layout->addLayout(fields);
    lineEdit->selectAll();

    auto indicators = new QHBoxLayout;
    for (int i = 0; i < 4; ++i) {
      auto check =
          new QCheckBox(i < 2 ? QStringLiteral("Enabled check") : QStringLiteral("Disabled check"));
      check->setChecked(i % 2 == 1);
      check->setEnabled(i < 2);
      indicators->addWidget(check);
    }
    for (int i = 0; i < 4; ++i) {
      auto radioButton =
          new QRadioButton(i < 2 ? QStringLiteral("Radio") : QStringLiteral("Disabled"));
      radioButton->setAutoExclusive(false);
      radioButton->setChecked(i % 2 == 1);
      radioButton->setEnabled(i < 2);
      indicators->addWidget(radioButton);
    }
    layout->addLayout(indicators);
    layout->addWidget(semanticSurface(QStringLiteral("normal")));
    layout->addWidget(semanticSurface(QStringLiteral("content")));

    auto middle = new QHBoxLayout;
    tree = new QTreeWidget;
    tree->setHeaderLabels({QStringLiteral("Tree / active and inactive selection")});
    tree->setMinimumHeight(160);
    tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    auto expanded = new QTreeWidgetItem(tree, {QStringLiteral("Expanded branch")});
    new QTreeWidgetItem(expanded, {QStringLiteral("Selected enabled row")});
    new QTreeWidgetItem(expanded, {QStringLiteral("Child row")});
    expanded->setExpanded(true);
    auto collapsed = new QTreeWidgetItem(tree, {QStringLiteral("Collapsed branch")});
    new QTreeWidgetItem(collapsed, {QStringLiteral("Hidden child")});
    for (int i = 0; i < 15; ++i) {
      new QTreeWidgetItem(tree, {QStringLiteral("Scrollable tree row %1").arg(i)});
    }
    tree->setCurrentItem(expanded->child(0));
    middle->addWidget(tree, 2);
    list = new QListWidget;
    list->addItems({QStringLiteral("List ordinary"), QStringLiteral("Selected list row"),
                    QStringLiteral("Disabled list row")});
    list->item(2)->setFlags(list->item(2)->flags() & ~Qt::ItemIsEnabled);
    for (int i = 0; i < 15; ++i) {
      list->addItem(QStringLiteral("Scrollable list row %1").arg(i));
    }
    list->setCurrentRow(1);
    middle->addWidget(list, 1);
    auto side = new QWidget;
    auto sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    auto innerTabs = new QTabWidget;
    innerTabs->addTab(new QLabel(QStringLiteral("Selected tab content")),
                      QStringLiteral("Selected"));
    innerTabs->addTab(new QLabel(QStringLiteral("Other tab content")), QStringLiteral("Other"));
    sideLayout->addWidget(innerTabs);
    auto toolbox = new QToolBox;
    toolbox->addItem(new QLabel(QStringLiteral("Expanded real QToolBox page")),
                     QStringLiteral("Expanded toolbox"));
    toolbox->addItem(new QLabel(QStringLiteral("Collapsed page")),
                     QStringLiteral("Collapsed toolbox"));
    sideLayout->addWidget(toolbox);
    auto progress = new QProgressBar;
    progress->setValue(63);
    sideLayout->addWidget(progress);
    auto scrollbar = new QScrollBar(Qt::Horizontal);
    scrollbar->setRange(0, 100);
    scrollbar->setValue(35);
    sideLayout->addWidget(scrollbar);
    middle->addWidget(side, 2);
    layout->addLayout(middle, 1);

    auto svgGrid = new QGridLayout;
    const QStringList names = {QStringLiteral("arrow_dropdown"),
                               QStringLiteral("branch_closed"),
                               QStringLiteral("branch_open"),
                               QStringLiteral("checkbox_checked"),
                               QStringLiteral("checkbox_unchecked"),
                               QStringLiteral("radiobutton_checked"),
                               QStringLiteral("radiobutton_unchecked"),
                               QStringLiteral("menu_checkbox"),
                               QStringLiteral("menu_radiobutton"),
                               QStringLiteral("close"),
                               QStringLiteral("down"),
                               QStringLiteral("expand_toolbar"),
                               QStringLiteral("float"),
                               QStringLiteral("left"),
                               QStringLiteral("right"),
                               QStringLiteral("sizegrip"),
                               QStringLiteral("up")};
    for (int i = 0; i < names.size(); ++i) {
      auto cell = new QWidget;
      auto cellLayout = new QHBoxLayout(cell);
      cellLayout->setContentsMargins(1, 1, 1, 1);
      for (const auto &suffix : {QString(), QStringLiteral("_disabled")}) {
        const auto path =
            assets.folder + QLatin1Char('/') + names[i] + suffix + QStringLiteral(".svg");
        if (!suffix.isEmpty() && !QFileInfo::exists(path)) {
          continue;
        }
        QSvgRenderer renderer(path);
        require(renderer.isValid(), QStringLiteral("Missing or invalid indicator: %1").arg(path));
        QPixmap pixmap(20, 20);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        renderer.render(&painter);
        painter.end();
        auto icon = new QLabel;
        icon->setPixmap(pixmap);
        icon->setFixedSize(20, 20);
        icon->setToolTip(names[i] + suffix);
        cellLayout->addWidget(icon);
      }
      auto captionText = names[i];
      captionText.replace(QLatin1Char('_'), QLatin1Char(' '));
      auto caption = new QLabel(captionText);
      caption->setMinimumWidth(0);
      caption->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
      caption->setWordWrap(true);
      caption->setToolTip(names[i] + QStringLiteral(": enabled, then disabled where supplied"));
      cellLayout->addWidget(caption);
      svgGrid->addWidget(cell, i / 6, i % 6);
    }
    layout->addLayout(svgGrid);
    auto status = new QStatusBar;
    status->setSizeGripEnabled(true);
    for (int column : {1, 3, 4, 5}) {
      QWidget *segment = nullptr;
      if (column == 3) {
        auto spelling = new QToolButton;
        spelling->setText(QStringLiteral("Spelling"));
        segment = spelling;
      } else {
        segment = new QLabel(column == 1   ? QStringLiteral("Ln 12, Col 8")
                             : column == 4 ? QStringLiteral("C++ syntax")
                                           : QStringLiteral("NORMAL mode"));
      }
      segment->setProperty("StatusBarCol", column);
      status->addWidget(segment);
    }
    layout->addWidget(status);
    QWidget::setTabOrder(hoverButton, actionButton);
    QWidget::setTabOrder(actionButton, dangerButton);
    QWidget::setTabOrder(dangerButton, pushButton);
    QWidget::setTabOrder(pushButton, lineEdit);
    return page;
  }

  QWidget *makeEditors() {
    auto splitter = new QSplitter(this);
    auto config = [this](bool markdown) {
      auto result = QSharedPointer<vte::TextEditorConfig>::create();
      // MarkdownEditorConfig mutates its theme: never share this instance.
      result->m_theme = vte::Theme::createThemeFromContent(assets.editor);
      require(bool(result->m_theme), QStringLiteral("Cannot load editor theme"));
      result->m_syntaxTheme = markdown ? assets.theme->getMarkdownEditorHighlightTheme()
                                       : assets.theme->getEditorHighlightTheme();
      result->m_lineNumberType = vte::VTextEditor::LineNumberType::Absolute;
      result->m_textFoldingEnabled = true;
      return result;
    };
    auto parameters = QSharedPointer<vte::TextEditorParameters>::create();
    parameters->m_spellCheckEnabled = false;
    textEditor = new vte::VTextEditor(config(false), parameters, splitter);
    textEditor->enableInternalContextMenu();
    textEditor->setText(QString::fromUtf8(cppSample));
    textEditor->setSyntax(QStringLiteral("cpp"));
    auto markdownConfig = QSharedPointer<vte::MarkdownEditorConfig>::create(config(true));
    markdownConfig->m_webCodeBlockHighlighterEnabled = false;
    markdownConfig->m_inplacePreviewSources = vte::MarkdownEditorConfig::NoInplacePreview;
    markdownConfig->m_concealElements = vte::MarkdownConcealElement::LinkUrl |
                                        vte::MarkdownConcealElement::ImageUrl |
                                        vte::MarkdownConcealElement::ReferenceUrl;
    markdownEditor = new vte::VMarkdownEditor(markdownConfig, parameters, splitter);
    markdownEditor->enableInternalContextMenu();
    connect(markdownEditor->getHighlighter(), &vte::MarkdownHighlighter::highlightCompleted, this,
            [this]() { markdownReady = true; });
    markdownEditor->setText(QString::fromUtf8(markdownSample));
    markdownEditor->getHighlighter()->updateHighlight();
    splitter->setSizes({620, 620});
    return splitter;
  }

  QWidget *makeReader(int highlightMode, bool tips) {
    const auto html = readerHtml(assets, highlightMode, tips);
    auto page = new QWidget(this);
    auto layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto options = new QWidget;
    auto optionsLayout = new QHBoxLayout(options);
    highlightSelector = new QComboBox;
    highlightSelector->addItems({QStringLiteral("Bundled highlight.css"),
                                 QStringLiteral("One Light (optional)"),
                                 QStringLiteral("Dark highlight (optional)")});
    highlightSelector->setCurrentIndex(highlightMode);
    tipsToggle = new QCheckBox(QStringLiteral("Packaged tip icons"));
    tipsToggle->setChecked(tips);
    optionsLayout->addWidget(highlightSelector);
    optionsLayout->addWidget(tipsToggle);
    optionsLayout->addStretch();
    options->setVisible(assets.name == QStringLiteral("vx-idea"));
    layout->addWidget(options);
    profile = new QWebEngineProfile(this);
    require(profile->isOffTheRecord(), QStringLiteral("Reader profile must be off the record"));
    profile->setHttpCacheType(QWebEngineProfile::NoCache);
    web = new QWebEngineView(page);
    web->setPage(new QWebEnginePage(profile, web));
    web->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
    web->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
    layout->addWidget(web, 1);
    connect(web, &QWebEngineView::loadFinished, this, [this](bool success) {
      webSucceeded = success;
      webFinished = true;
    });
    web->setHtml(html, QUrl::fromLocalFile(webRoot + QLatin1Char('/')));
    return page;
  }
};

bool hasPixelVariation(const QPixmap &pixmap) {
  if (pixmap.isNull()) {
    return false;
  }
  const auto image = pixmap.toImage().convertToFormat(QImage::Format_RGB32);
  const QRgb initial = image.pixel(image.width() / 2, image.height() / 2);
  int differences = 0;
  const int inset = qRound(20 * pixmap.devicePixelRatio());
  for (int y = inset; y < image.height() - inset; y += 3) {
    for (int x = inset; x < image.width() - inset; x += 3) {
      if (image.pixel(x, y) != initial && ++differences > 100) {
        return true;
      }
    }
  }
  return false;
}

class DemoWindow : public QMainWindow {
public:
  DemoWindow() {
    setWindowTitle(QStringLiteral("VNote theme visibility demo"));
    resize(1280, 900);
    auto toolbar = addToolBar(QStringLiteral("Theme source"));
    toolbar->setMovable(false);
    selector = new QComboBox;
    selector->addItems(themeNames());
    toolbar->addWidget(selector);
    reload = new QPushButton(QStringLiteral("Reload"));
    toolbar->addWidget(reload);
    sourceLabel = new QLabel;
    sourceLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    toolbar->addWidget(sourceLabel);
    connect(selector, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
      if (!applying) {
        applyInteractive(selector->currentText(), 0, false);
      }
    });
    connect(reload, &QPushButton::clicked, this, [this]() {
      if (view && !applying) {
        applyInteractive(view->assets.name, view->highlightSelector->currentIndex(),
                         view->tipsToggle->isChecked());
      }
    });
  }

  void applyTheme(QString name, int highlightMode = 0, bool tips = false) {
    require(!applying, QStringLiteral("Theme reload already in progress"));
    applying = true;
    reload->setEnabled(false);
    selector->setEnabled(false);
    auto previous = view;
    const auto previousStyle = qApp->styleSheet();
    const int previousTab = previous ? previous->tabs->currentIndex() : 0;
    try {
      auto assets = loadAssets(name);
      // Construct before detaching the old view. Any resource failure here
      // leaves the previous live editor/page and stylesheet untouched.
      auto candidate = std::make_unique<DemoView>(std::move(assets), highlightMode, tips);
      if (previous) {
        takeCentralWidget();
        previous->hide();
      }
      view = candidate.release();
      setCentralWidget(view);
      QPixmapCache::clear();
      qApp->setStyleSheet(view->assets.qss);
      view->show();
      show();
      raise();
      activateWindow();
      require(QTest::qWaitForWindowExposed(this, readinessTimeout),
              QStringLiteral("Demo window was not exposed"));
      view->waitForEditors();
      view->waitForReader();
      view->tabs->setCurrentIndex(previousTab);
      paintTurn();
      connect(view->highlightSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
              [this]() {
                if (!applying) {
                  applyInteractive(view->assets.name, view->highlightSelector->currentIndex(),
                                   view->tipsToggle->isChecked());
                }
              });
      connect(view->tipsToggle, &QCheckBox::toggled, this, [this]() {
        if (!applying) {
          applyInteractive(view->assets.name, view->highlightSelector->currentIndex(),
                           view->tipsToggle->isChecked());
        }
      });
      if (previous) {
        previous->deleteLater();
      }
      currentHighlightMode = highlightMode;
      currentTips = tips;
      sourceLabel->setText(QStringLiteral("  %1  |  %2").arg(name, view->assets.folder));
      setWindowTitle(QStringLiteral("VNote theme visibility - %1").arg(name));
      const QSignalBlocker blocker(selector);
      selector->setCurrentText(name);
      applying = false;
      selector->setEnabled(true);
      reload->setEnabled(true);
      QTextStream output(stdout);
      output << "READY " << name << '\n';
      output.flush();
    } catch (...) {
      if (view != previous) {
        delete takeCentralWidget();
        view = previous;
        qApp->setStyleSheet(previousStyle);
        if (previous) {
          setCentralWidget(previous);
          previous->show();
        }
      }
      if (previous) {
        const QSignalBlocker selectorBlocker(selector);
        const QSignalBlocker modeBlocker(previous->highlightSelector);
        const QSignalBlocker tipsBlocker(previous->tipsToggle);
        selector->setCurrentText(previous->assets.name);
        previous->highlightSelector->setCurrentIndex(currentHighlightMode);
        previous->tipsToggle->setChecked(currentTips);
      }
      applying = false;
      selector->setEnabled(true);
      reload->setEnabled(true);
      throw;
    }
  }

  void captureTheme(const QString &name, const QString &directory) {
    applyTheme(name);
    activate();
    view->tabs->setCurrentIndex(0);
    moveMouse(view->tabs->tabBar(), QPoint(4, 4));
    view->lineEdit->setFocus();
    view->lineEdit->selectAll();
    save(directory, QStringLiteral("controls-normal"));
    auto *progress = view->findChild<QProgressBar *>();
    require(progress != nullptr, QStringLiteral("Missing progress fixture"));
    const int previousProgress = progress->value();
    for (int value : {0, 100}) {
      progress->setValue(value);
      save(directory, QStringLiteral("controls-progress-%1").arg(value));
    }
    progress->setValue(previousProgress);
    moveMouse(view->hoverButton, view->hoverButton->rect().center());
    waitUntil([this]() { return view->hoverButton->underMouse(); },
              QStringLiteral("tool button hover"));
    save(directory, QStringLiteral("controls-hover"));
    QTest::mousePress(view->hoverButton, Qt::LeftButton);
    require(view->hoverButton->isDown(), QStringLiteral("Tool button did not enter pressed state"));
    try {
      save(directory, QStringLiteral("controls-pressed"));
    } catch (...) {
      QTest::mouseRelease(view->hoverButton, Qt::LeftButton);
      throw;
    }
    QTest::mouseRelease(view->hoverButton, Qt::LeftButton);
    view->hoverButton->setChecked(true);
    moveMouse(view->tabs->tabBar(), QPoint(4, 4));
    view->hoverButton->setFocus();
    QTest::keyClick(view->hoverButton, Qt::Key_Tab);
    require(view->actionButton->hasFocus(),
            QStringLiteral("Tab navigation did not focus ActionToolButton"));
    save(directory, QStringLiteral("controls-focus"));
    QTest::keyClick(view->actionButton, Qt::Key_Tab);
    require(view->dangerButton->hasFocus(),
            QStringLiteral("Tab navigation did not focus DangerButton"));
    moveMouse(view->dangerButton, view->dangerButton->rect().center());
    waitUntil([this]() { return view->dangerButton->underMouse(); },
              QStringLiteral("focused danger button hover"));
    save(directory, QStringLiteral("controls-danger-focus-hover"));
    QTest::keyClick(view->dangerButton, Qt::Key_Tab);
    require(view->pushButton->hasFocus(),
            QStringLiteral("Tab navigation did not focus checked push button"));
    moveMouse(view->pushButton, view->pushButton->rect().center());
    waitUntil([this]() { return view->pushButton->underMouse(); },
              QStringLiteral("checked push button hover"));
    save(directory, QStringLiteral("controls-push-hover"));
    QTest::mousePress(view->pushButton, Qt::LeftButton);
    require(view->pushButton->isDown(), QStringLiteral("Push button did not enter pressed state"));
    try {
      save(directory, QStringLiteral("controls-push-pressed"));
    } catch (...) {
      QTest::mouseRelease(view->pushButton, Qt::LeftButton);
      throw;
    }
    QTest::mouseRelease(view->pushButton, Qt::LeftButton);
    view->pushButton->setChecked(true);
    moveMouse(view->tabs->tabBar(), QPoint(4, 4));

    QWidget inactiveWindow(nullptr, Qt::Window);
    inactiveWindow.setWindowTitle(QStringLiteral("Inactive-selection activation target"));
    inactiveWindow.resize(220, 90);
    auto activationLayout = new QVBoxLayout(&inactiveWindow);
    activationLayout->addWidget(new QLabel(QStringLiteral("Main demo is now inactive")));
    // Prefer an adjacent position; the saved widget excludes this window.
    inactiveWindow.move(frameGeometry().topRight() + QPoint(12, 0));
    inactiveWindow.show();
    inactiveWindow.raise();
    inactiveWindow.activateWindow();
    require(QTest::qWaitForWindowActive(&inactiveWindow, readinessTimeout),
            QStringLiteral("Inactive-selection target did not activate"));
    waitUntil([this]() { return !isActiveWindow(); }, QStringLiteral("main window inactive state"));
    save(directory, QStringLiteral("controls-inactive"));
    inactiveWindow.hide();
    activate();

    view->menu->popup(view->menuButton->mapToGlobal(QPoint(0, view->menuButton->height())));
    waitUntil([this]() { return view->menu->isVisible(); }, QStringLiteral("menu exposure"));
    const auto actionRect = view->menu->actionGeometry(view->menu->actions().at(1));
    moveMouse(view->menu, actionRect.center());
    save(directory, QStringLiteral("controls-menu"), false, true);
    view->menu->hide();

    view->waitForEditors();
    save(directory, QStringLiteral("editors"));
    for (auto editor : {view->textEditor, static_cast<vte::VTextEditor *>(view->markdownEditor)}) {
      QTextCursor cursor(editor->document());
      cursor.movePosition(QTextCursor::Start);
      cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor, 12);
      editor->getTextEdit()->setTextCursor(cursor);
      require(editor->getTextEdit()->textCursor().hasSelection(),
              QStringLiteral("Editor selection did not take effect"));
      editor->scrollToLine(0, false);
    }
    view->markdownEditor->getTextEdit()->setFocus();
    save(directory, QStringLiteral("editors-selected"));
    captureReader(directory, QString());
    if (name == QStringLiteral("vx-idea")) {
      for (int mode : {1, 2}) {
        applyTheme(name, mode, false);
        captureReader(directory,
                      mode == 1 ? QStringLiteral("-one-light") : QStringLiteral("-dark"));
      }
      applyTheme(name, 0, true);
      captureReader(directory, QStringLiteral("-tips"));
    }
  }

private:
  DemoView *view = nullptr;
  QComboBox *selector = nullptr;
  QPushButton *reload = nullptr;
  QLabel *sourceLabel = nullptr;
  bool applying = false;
  int currentHighlightMode = 0;
  bool currentTips = false;

  void applyInteractive(QString name, int mode, bool tips) {
    try {
      applyTheme(name, mode, tips);
    } catch (const std::exception &error) {
      QMessageBox box(QMessageBox::Critical, QStringLiteral("Theme reload failed"),
                      QString::fromUtf8(error.what()), QMessageBox::Ok, this);
      box.setTextFormat(Qt::PlainText);
      box.exec();
    }
  }

  void activate() {
    raise();
    activateWindow();
    require(QTest::qWaitForWindowActive(this, readinessTimeout),
            QStringLiteral("Demo window did not activate"));
    paintTurn();
  }

  void readerState(const QString &code) {
    javascript(view->web->page(),
               QStringLiteral("window.__themeDemoStatePainted = false;\n") + code +
                   QStringLiteral("\nrequestAnimationFrame(function(){requestAnimationFrame("
                                  "function(){window.__themeDemoStatePainted=true;});}); true;"));
    QElapsedTimer timer;
    timer.start();
    while (!javascript(view->web->page(), QStringLiteral("window.__themeDemoStatePainted === true"))
                .toBool()) {
      require(timer.elapsed() < readinessTimeout,
              QStringLiteral("Timed out waiting for reader state paint"));
    }
    paintTurn();
  }

  void captureSectionRemainder(const QString &directory, const QString &state,
                               const QString &section, const QString &suffix) {
    int pageNumber = 2;
    while (true) {
      const auto geometry =
          javascript(view->web->page(),
                     QStringLiteral(
                         "({bottom:document.getElementById('%1').getBoundingClientRect().bottom,"
                         "viewport:innerHeight,scroll:scrollY,maximum:document.documentElement."
                         "scrollHeight-innerHeight})")
                         .arg(section))
              .toMap();
      if (geometry.value(QStringLiteral("bottom")).toDouble() <=
              geometry.value(QStringLiteral("viewport")).toDouble() + 2 ||
          geometry.value(QStringLiteral("scroll")).toDouble() >=
              geometry.value(QStringLiteral("maximum")).toDouble() - 2) {
        break;
      }
      const auto previousScroll = geometry.value(QStringLiteral("scroll")).toDouble();
      readerState(QStringLiteral("window.scrollBy(0,innerHeight*0.8);"));
      require(javascript(view->web->page(), QStringLiteral("scrollY")).toDouble() > previousScroll,
              QStringLiteral("Reader section could not scroll: %1").arg(section));
      save(directory, state + suffix + QStringLiteral("-page-%1").arg(pageNumber++), true);
    }
  }

  void captureReader(const QString &directory, const QString &suffix) {
    activate();
    view->waitForReader();
    readerState(QStringLiteral("window.getSelection().removeAllRanges(); window.scrollTo(0,0);"));
    save(directory, QStringLiteral("reader") + suffix, true);
    readerState(QStringLiteral(R"JS(var range = document.createRange();
            range.selectNodeContents(document.getElementById('reader-prose'));
            window.getSelection().removeAllRanges(); window.getSelection().addRange(range);
            document.getElementById('reader-prose').scrollIntoView(); )JS"));
    require(
        javascript(view->web->page(),
                   QStringLiteral("window.getSelection().toString().includes('Ordinary prose')"))
            .toBool(),
        QStringLiteral("Reader selection is empty"));
    save(directory, QStringLiteral("reader-selected") + suffix, true);
    readerState(QStringLiteral("window.getSelection().removeAllRanges(); "
                               "document.getElementById('reader-alerts').scrollIntoView();"));
    save(directory, QStringLiteral("reader-alerts") + suffix, true);
    captureSectionRemainder(directory, QStringLiteral("reader-alerts"),
                            QStringLiteral("reader-alerts"), suffix);
    readerState(QStringLiteral("document.getElementById('reader-code').scrollIntoView();"));
    // Hover a real Prism code toolbar so its production opacity transition is visible.
    const auto point = javascript(view->web->page(), QStringLiteral(R"JS((function() {
            var r = document.querySelector('#reader-code div.code-toolbar').getBoundingClientRect();
            return {x:Math.min(innerWidth-30,r.right-30),y:r.top+15};
        })())JS"))
                           .toMap();
    moveMouse(view->web, QPoint(point.value(QStringLiteral("x")).toInt(),
                                point.value(QStringLiteral("y")).toInt()));
    auto *delegate = view->web->focusProxy();
    require(delegate != nullptr, QStringLiteral("Reader has no input delegate"));
    const QPoint pointGlobal = view->web->mapToGlobal(
        QPoint(point.value(QStringLiteral("x")).toInt(), point.value(QStringLiteral("y")).toInt()));
    QMouseEvent pointMove(QEvent::MouseMove, delegate->mapFromGlobal(pointGlobal), pointGlobal,
                          Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(delegate, &pointMove);
    waitUntil(
        [this]() {
          return javascript(view->web->page(),
                            QStringLiteral("document.querySelector('#reader-code div.code-toolbar')"
                                           ".matches(':hover')"))
              .toBool();
        },
        QStringLiteral("reader code toolbar hover"));
    QTest::qWait(350);
    readerState(QString());
    save(directory, QStringLiteral("reader-code") + suffix, true);
    // Selection must also cover colored Prism descendants, not only prose.
    readerState(QStringLiteral(R"JS(var range = document.createRange();
            range.selectNodeContents(document.querySelector('#reader-code code.language-cpp'));
            window.getSelection().removeAllRanges(); window.getSelection().addRange(range);)JS"));
    save(directory, QStringLiteral("reader-code-selected") + suffix, true);
    readerState(QStringLiteral("window.getSelection().removeAllRanges();"));
    captureSectionRemainder(directory, QStringLiteral("reader-code"), QStringLiteral("reader-code"),
                            suffix);
    readerState(QStringLiteral("window.getSelection().removeAllRanges(); "
                               "document.getElementById('reader-table').scrollIntoView();"));
    const auto row = javascript(view->web->page(), QStringLiteral(R"JS((function(){
            var r=document.getElementById('table-hover').getBoundingClientRect();return {x:r.left+20,y:r.top+10};
        })())JS"))
                         .toMap();
    moveMouse(view->web, QPoint(row.value(QStringLiteral("x")).toInt(),
                                row.value(QStringLiteral("y")).toInt()));
    const QPoint rowGlobal = view->web->mapToGlobal(
        QPoint(row.value(QStringLiteral("x")).toInt(), row.value(QStringLiteral("y")).toInt()));
    QMouseEvent rowMove(QEvent::MouseMove, delegate->mapFromGlobal(rowGlobal), rowGlobal,
                        Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(delegate, &rowMove);
    waitUntil(
        [this]() {
          return javascript(
                     view->web->page(),
                     QStringLiteral("document.getElementById('table-hover').matches(':hover')"))
              .toBool();
        },
        QStringLiteral("reader table hover"));
    readerState(QString());
    save(directory, QStringLiteral("reader-table-hover") + suffix, true);
    readerState(QStringLiteral("document.getElementById('reader-graphs').scrollIntoView();"));
    save(directory, QStringLiteral("reader-graphs") + suffix, true);
  }

  void saveReaderAudit(const QString &path, const QString &state) {
    const auto values = javascript(view->web->page(), QStringLiteral(R"JS((function() {
            function identity(element) {
                return element.tagName.toLowerCase() + (element.id ? '#' + element.id : '')
                    + (typeof element.className === 'string' && element.className ? '.' + element.className.trim().replace(/\s+/g, '.') : '');
            }
            function rgba(value) {
                var parts = value.match(/[\d.]+/g);
                if (!parts || parts.length < 3) return null;
                return [+parts[0], +parts[1], +parts[2], parts.length > 3 ? +parts[3] : 1];
            }
            function over(foreground, background) {
                var alpha = foreground[3] + background[3] * (1 - foreground[3]);
                if (!alpha) return [0, 0, 0, 0];
                return [0, 1, 2].map(function(i) {
                    return (foreground[i] * foreground[3] + background[i] * background[3] * (1 - foreground[3])) / alpha;
                }).concat([alpha]);
            }
            function layers(element) {
                var result = [];
                for (var item = element; item; item = item.parentElement) {
                    var style = getComputedStyle(item);
                    result.push({element:identity(item),color:style.color,backgroundColor:style.backgroundColor,
                        backgroundImage:style.backgroundImage,opacity:style.opacity});
                }
                return result;
            }
            function effectiveBackground(ancestors) {
                var result = [0, 0, 0, 0];
                for (var i = ancestors.length - 1; i >= 0; --i) {
                    var color = rgba(ancestors[i].backgroundColor);
                    if (color) result = over(color, result);
                }
                return result;
            }
            function colors(style) {
                return {color:style.color,backgroundColor:style.backgroundColor,backgroundImage:style.backgroundImage,
                    opacity:style.opacity,fontFamily:style.fontFamily,fontSize:style.fontSize,fontWeight:style.fontWeight,
                    textShadow:style.textShadow,textDecoration:style.textDecoration,outlineColor:style.outlineColor,
                    outlineWidth:style.outlineWidth,fill:style.fill,stroke:style.stroke,
                    borderTopColor:style.borderTopColor,borderTopWidth:style.borderTopWidth,
                    borderRightColor:style.borderRightColor,borderRightWidth:style.borderRightWidth,
                    borderBottomColor:style.borderBottomColor,borderBottomWidth:style.borderBottomWidth,
                    borderLeftColor:style.borderLeftColor,borderLeftWidth:style.borderLeftWidth};
            }
            var nodes = [];
            document.querySelectorAll('#vx-content, #vx-content *').forEach(function(element) {
                var directText = Array.prototype.filter.call(element.childNodes, function(node) {
                    return node.nodeType === Node.TEXT_NODE && node.textContent.trim();
                }).map(function(node) { return node.textContent.trim(); }).join(' ');
                var before = getComputedStyle(element, '::before');
                var hasBefore = before.content && before.content !== 'none' && before.content !== 'normal';
                if (!directText && !hasBefore && !element.matches('svg, svg *, .vx-codeblock-action-btn')) return;
                var ancestors = layers(element), rect = element.getBoundingClientRect();
                var entry = {element:identity(element),text:directText,style:colors(getComputedStyle(element)),
                    selection:colors(getComputedStyle(element, '::selection')),ancestorLayers:ancestors,
                    effectiveBackgroundRgba:effectiveBackground(ancestors),
                    visibleInViewport:rect.width > 0 && rect.height > 0 && rect.bottom > 0 && rect.top < innerHeight,
                    rect:{x:rect.x,y:rect.y,width:rect.width,height:rect.height}};
                if (hasBefore) {
                    entry.before = {content:before.content,style:colors(before),
                        effectiveBackgroundRgba:over(rgba(before.backgroundColor) || [0,0,0,0], entry.effectiveBackgroundRgba)};
                }
                nodes.push(entry);
            });
            return {nodes:nodes,scrollX:scrollX,scrollY:scrollY,viewport:{width:innerWidth,height:innerHeight},
                selectedText:window.getSelection().toString(),fontsStatus:document.fonts.status,
                notes:['Colors come from getComputedStyle, never antialiased pixels.',
                    'effectiveBackgroundRgba composites background colors only; ancestor opacity and backgroundImage remain explicit for audit.',
                    'Visited styles are privacy-masked: use declaredThemeCss and inspect the visited-link rendering.']};
        })())JS"));
    auto object = QJsonObject::fromVariantMap(values.toMap());
    require(!object.value(QStringLiteral("nodes")).toArray().isEmpty(),
            QStringLiteral("Reader style audit is empty"));
    object.insert(QStringLiteral("theme"), view->assets.name);
    object.insert(QStringLiteral("state"), state);
    object.insert(QStringLiteral("declaredThemeCss"), view->assets.web);
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::NewOnly),
            QStringLiteral("Cannot create reader audit: %1").arg(path));
    require(file.write(bytes) == bytes.size() && file.flush(),
            QStringLiteral("Cannot write reader audit: %1").arg(path));
  }

  void save(const QString &directory, const QString &state, bool reader = false,
            bool withMenu = false) {
    if (reader) {
      // DOM animation frames precede WebEngine's native texture submission.
      // Flush a delegate paint after the compositor and CSS transitions settle.
      QTest::qWait(350);
      if (auto *delegate = view->web->focusProxy()) {
        delegate->repaint();
      }
    }
    paintTurn();
    auto image = grab();
    require(!image.isNull(), QStringLiteral("Null window capture: %1").arg(state));
    if (reader) {
      require(view->tabs->currentIndex() == 2 && view->web->isVisible(),
              QStringLiteral("Reader must be exposed during capture"));
      if (!hasPixelVariation(view->web->grab())) {
        auto screen = windowHandle()->screen();
        require(screen != nullptr, QStringLiteral("No screen available for WebEngine capture"));
        image = screen->grabWindow(winId());
        const qreal scale = image.devicePixelRatio();
        const QPoint topLeft = view->web->mapTo(this, QPoint());
        const QRect webRect(qRound(topLeft.x() * scale), qRound(topLeft.y() * scale),
                            qRound(view->web->width() * scale),
                            qRound(view->web->height() * scale));
        require(hasPixelVariation(image.copy(webRect)),
                QStringLiteral("WebEngine capture is blank (including screen fallback)"));
      }
    }
    if (withMenu) {
      // Popup menus are separate native windows, so include their real grab.
      const auto popup = view->menu->grab();
      require(!popup.isNull(), QStringLiteral("Null menu capture"));
      QPainter painter(&image);
      painter.drawPixmap(mapFromGlobal(view->menu->mapToGlobal(QPoint())), popup);
    }
    const auto path = QDir(directory).filePath(view->assets.name + QLatin1Char('-') + state +
                                               QStringLiteral(".png"));
    require(!QFileInfo::exists(path),
            QStringLiteral("Refusing to overwrite capture: %1").arg(path));
    QFile png(path);
    require(png.open(QIODevice::WriteOnly | QIODevice::NewOnly),
            QStringLiteral("Cannot create capture: %1").arg(path));
    require(image.save(&png, "PNG") && png.flush(),
            QStringLiteral("Cannot save capture: %1").arg(path));
    png.close();
    if (reader) {
      saveReaderAudit(path.left(path.size() - 4) + QStringLiteral(".styles.json"), state);
    }
    QTextStream output(stdout);
    output << "CAPTURED " << view->assets.name << ' ' << state << ' ' << path << '\n';
    output.flush();
  }
};

QString prepareCaptureDirectory(const QString &path) {
  require(!path.trimmed().isEmpty(), QStringLiteral("Capture destination must not be empty"));
  const QFileInfo info(path);
  require(!info.exists() || info.isDir(),
          QStringLiteral("Capture destination is not a directory: %1").arg(path));
  QDir directory(info.absoluteFilePath());
  if (directory.exists()) {
    require(
        directory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)
            .isEmpty(),
        QStringLiteral("Capture destination must be absent or empty: %1").arg(path));
  } else {
    require(QDir().mkpath(directory.absolutePath()),
            QStringLiteral("Cannot create capture destination: %1").arg(path));
  }
  return directory.absolutePath();
}

} // namespace

int main(int argc, char *argv[]) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
  QApplication app(argc, argv);
  app.setApplicationName(QStringLiteral("vnote-theme-demo"));
  QCommandLineParser parser;
  parser.setApplicationDescription(QStringLiteral(
      "Source-theme Qt, VTextEdit and WebEngine visibility demo (no user configuration)."));
  parser.addHelpOption();
  QCommandLineOption themeOption(
      QStringLiteral("theme"), QStringLiteral("Select an included theme."), QStringLiteral("name"));
  QCommandLineOption captureOption(
      QStringLiteral("capture-dir"),
      QStringLiteral("Capture all themes, or only --theme, into an absent/empty directory."),
      QStringLiteral("directory"));
  parser.addOption(themeOption);
  parser.addOption(captureOption);
  if (!parser.parse(app.arguments()) || !parser.positionalArguments().isEmpty()) {
    QTextStream(stderr) << parser.errorText() << '\n' << parser.helpText();
    return 2;
  }
  if (parser.isSet(QStringLiteral("help"))) {
    QTextStream(stdout) << parser.helpText();
    return 0;
  }
  const auto selected =
      parser.isSet(themeOption) ? parser.value(themeOption) : themeNames().first();
  if (!themeNames().contains(selected)) {
    QTextStream(stderr) << "Unknown or excluded theme: " << selected << '\n';
    return 2;
  }
  try {
    vte::VTextEditor::addSyntaxCustomSearchPaths(
        {sourceRoot + QStringLiteral("/src/data/extra/syntax-highlighting")});
    DemoWindow window;
    const bool capture = parser.isSet(captureOption);
    const QString directory =
        capture ? prepareCaptureDirectory(parser.value(captureOption)) : QString();
    // Run setup within the event loop so WebEngine and native exposure are live.
    QTimer::singleShot(0, &window, [&]() {
      try {
        window.show();
        if (capture) {
          const auto names = parser.isSet(themeOption) ? QStringList{selected} : themeNames();
          for (const auto &name : names) {
            window.captureTheme(name, directory);
          }
          app.exit(0);
        } else {
          window.applyTheme(selected);
        }
      } catch (const std::exception &error) {
        QTextStream(stderr) << "ERROR " << error.what() << '\n';
        app.exit(1);
      }
    });
    return app.exec();
  } catch (const std::exception &error) {
    QTextStream(stderr) << "ERROR " << error.what() << '\n';
    return 1;
  }
}
