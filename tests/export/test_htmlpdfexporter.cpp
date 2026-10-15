// SPDX-License-Identifier: LGPL-3.0-or-later

#include <QtTest>

#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QHostAddress>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPageLayout>
#include <QPageSize>
#include <QPointer>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineView>

#include <core/servicelocator.h>
#include <export/htmlpdfexporter.h>
#include <export/mindmapexporter.h>
#include <vxcore/vxcore.h>

#include <cmath>
#include <memory>
#include <utility>

using namespace vnotex;

namespace tests {
namespace {

QByteArray readFile(const QString &p_path) {
  QFile file(p_path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeFile(const QString &p_path, const QByteArray &p_bytes) {
  QFile file(p_path);
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
         file.write(p_bytes) == p_bytes.size();
}

QByteArray imageBytes() {
  QImage image(123, 67, QImage::Format_RGB32);
  image.fill(QColor(241, 11, 211));
  for (int y = 16; y < 51; ++y) {
    for (int x = 30; x < 93; ++x) {
      image.setPixelColor(x, y, QColor(7, 223, 191));
    }
  }
  QByteArray bytes;
  QBuffer buffer(&bytes);
  if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
    return {};
  }
  return bytes;
}

QStringList topics() {
  return {QString::fromUtf8("Root 世界 & <map>"),
          QStringLiteral("Level two"),
          QString::fromUtf8("Level three\nstyled 世界 <&>"),
          QStringLiteral("Level four"),
          QStringLiteral("Level five"),
          QStringLiteral("Level six"),
          QStringLiteral("Level seven"),
          QString::fromUtf8("Deep café eight"),
          QStringLiteral("Separate orphan tree"),
          QString::fromUtf8("שלום 42 orphan")};
}

QString longNote() {
  QString note = QStringLiteral("LONG_NOTE_BEGIN\n");
  for (int i = 0; i < 96; ++i) {
    note += QStringLiteral("Paragraph %1: readable details remain complete across printed pages; "
                           "neither deep nesting nor map fitting may truncate this sentence.\n")
                .arg(i);
  }
  return note + QString::fromUtf8("LONG_NOTE_END — résumé 世界");
}

ExportInput nativeInput(const QString &p_directory) {
  const auto labels = topics();
  QJsonArray nodes;
  for (int i = 0; i < labels.size(); ++i) {
    QJsonObject node{{QStringLiteral("id"), QStringLiteral("n%1").arg(i)},
                     {QStringLiteral("topic"), labels.at(i)}};
    if (i < 7 || i == 8) {
      node.insert(QStringLiteral("children"), QJsonArray{QStringLiteral("n%1").arg(i + 1)});
    }
    if (i == 1) {
      node.insert(QStringLiteral("expanded"), false);
    }
    if (i == 2) {
      node.insert(QStringLiteral("style"),
                  QJsonObject{{QStringLiteral("fontSize"), 23},
                              {QStringLiteral("fontWeight"), 700},
                              {QStringLiteral("fontStyle"), QStringLiteral("italic")},
                              {QStringLiteral("color"), QStringLiteral("#5729a5")},
                              {QStringLiteral("background"), QStringLiteral("#fce8b8")},
                              {QStringLiteral("branchColor"), QStringLiteral("#ca3542")}});
    }
    if (i == 0) {
      node.insert(QStringLiteral("image"),
                  QJsonObject{{QStringLiteral("url"), QStringLiteral("fixture.png")},
                              {QStringLiteral("width"), 123},
                              {QStringLiteral("height"), 67}});
    }
    if (i == 7) {
      node.insert(QStringLiteral("note"), longNote());
      node.insert(QStringLiteral("hyperLink"),
                  QStringLiteral("https://example.invalid/deep?q=1&x=2"));
    }
    nodes.append(node);
  }
  const QJsonObject document{
      {QStringLiteral("schemaVersion"), 1},
      {QStringLiteral("rootId"), QStringLiteral("n0")},
      {QStringLiteral("orphanIds"), QJsonArray{QStringLiteral("n8")}},
      {QStringLiteral("nodes"), nodes},
      {QStringLiteral("crossLinks"),
       QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("forest-link")},
                              {QStringLiteral("source"), QStringLiteral("n7")},
                              {QStringLiteral("target"), QStringLiteral("n9")},
                              {QStringLiteral("directed"), true},
                              {QStringLiteral("topic"), QStringLiteral("Across the forest")}}}}};
  ExportInput input;
  input.file.filePath = QDir(p_directory).filePath(QStringLiteral("forest.emind"));
  input.file.fileName = QStringLiteral("forest.emind");
  input.file.resourcePath = p_directory;
  input.file.fileTypeName = QStringLiteral("MindMap");
  input.content = QString::fromUtf8(QJsonDocument(document).toJson(QJsonDocument::Compact));
  return input;
}

ExportPdfOption pdfOption(QPageLayout::Orientation p_orientation = QPageLayout::Portrait) {
  ExportPdfOption option;
  option.m_useWkhtmltopdf = false;
  option.m_layout.reset(new QPageLayout(QPageSize(QPageSize::A4), p_orientation,
                                        QMarginsF(13, 17, 19, 23), QPageLayout::Millimeter));
  return option;
}

QVariant evaluateJavaScript(QWebEnginePage &p_page, const QString &p_script) {
  const auto state = std::make_shared<std::pair<bool, QVariant>>(false, QVariant());
  p_page.runJavaScript(p_script, [state](const QVariant &p_result) {
    state->second = p_result;
    state->first = true;
  });
  return QTest::qWaitFor([state] { return state->first; }, 10000) ? state->second : QVariant();
}

// All callbacks retain only heap-owned state; a timeout or page destruction cannot leave a
// JavaScript callback referring to a helper's return value or stack completion flag.
bool loadFile(QWebEnginePage &p_page, const QString &p_path) {
  const auto state = std::make_shared<std::pair<bool, bool>>(false, false);
  const auto connection =
      QObject::connect(&p_page, &QWebEnginePage::loadFinished, &p_page, [state](bool p_ok) {
        state->second = p_ok;
        state->first = true;
      });
  const auto cleanup = qScopeGuard([connection] { QObject::disconnect(connection); });
  p_page.load(QUrl::fromLocalFile(p_path));
  if (!QTest::qWaitFor([state] { return state->first; }, 15000) || !state->second) {
    return false;
  }
  if (!evaluateJavaScript(p_page, QStringLiteral(R"JS((() => {
    window.testResourcesReady = false;
    Promise.all(Array.from(document.images, image => image.decode())
      .concat([document.fonts.ready])).then(() => { window.testResourcesReady = true; });
    return true;
  })())JS"))
           .toBool()) {
    return false;
  }
  const QPointer<QWebEnginePage> page(&p_page);
  return QTest::qWaitFor(
      [page] {
        return page &&
               evaluateJavaScript(*page, QStringLiteral("window.testResourcesReady === true"))
                   .toBool();
      },
      15000);
}

struct BrowserPage {
  QWebEngineProfile profile;
  QWebEnginePage page{&profile};
  QWebEngineView view;

  BrowserPage() {
    page.settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
    view.setPage(&page);
    view.resize(1200, 900);
    view.show();
  }
};

QJsonObject documentState(QWebEnginePage &p_page) {
  const auto result = evaluateJavaScript(p_page, QStringLiteral(R"JS((() => {
    const visual = document.querySelector('#visual-map');
    const map = document.querySelector('#mindmap');
    const viewport = document.querySelector('#map-viewport');
    const svg = map.querySelector('.m3-connectors');
    const image = map.querySelector('.m3-node-image');
    const articles = document.querySelector('#articles');
    const button = document.querySelector('#view-toggle');
    const visible = element => {
      const box = element.getBoundingClientRect();
      return box.width > 0 && box.height > 0 && getComputedStyle(element).visibility !== 'hidden';
    };
    const rect = element => {
      const box = element.getBoundingClientRect();
      return {left:box.left, top:box.top, right:box.right, bottom:box.bottom,
              width:box.width, height:box.height};
    };
    const inside = (inner, outer) => inner.left >= outer.left - 2 &&
      inner.top >= outer.top - 2 && inner.right <= outer.right + 2 && inner.bottom <= outer.bottom + 2;
    const entries = Array.from(articles.querySelectorAll('article'));
    const deep = entries.find(entry => entry.querySelector('h3').textContent === 'Deep café eight');
    const note = deep.querySelector('p');
    const nodes = Array.from(map.querySelectorAll('.m3-node'));
    const rootTopic = nodes[0].querySelector('.m3-topic');
    const styledNode = nodes.find(node => node.dataset.nodeId === 'n2');
    const styledTopic = styledNode.querySelector('.m3-topic');
    const topicStyle = getComputedStyle(styledTopic.firstElementChild || styledTopic);
    return JSON.stringify({
      mapVisible:visible(map), articlesVisible:visible(articles), toggleVisible:visible(button),
      decoded:image.complete && image.naturalWidth > 0,
      source:image.currentSrc, imageWidth:image.naturalWidth, imageHeight:image.naturalHeight,
      naturalWidth:Number(map.dataset.width), naturalHeight:Number(map.dataset.height),
      map:rect(map), mapViewport:rect(viewport), visual:rect(visual), articles:rect(articles),
      topics:nodes.map(node => node.querySelector('.m3-topic').innerText.replace(/\s+/g, ' ').trim()),
      nodeIds:nodes.map(node => node.dataset.nodeId), rootTopic:rect(rootTopic),
      topicUserSelect:getComputedStyle(rootTopic).userSelect,
      rtlDirection:getComputedStyle(nodes.find(node => node.dataset.nodeId === 'n9')
        .querySelector('.m3-topic').firstElementChild).direction,
      svgNamespace:svg.namespaceURI, svgPointerEvents:getComputedStyle(svg).pointerEvents,
      treeEdges:svg.querySelectorAll('.m3-tree-edge').length,
      crossLinks:svg.querySelectorAll('.m3-cross-link').length,
      rasterSurfaces:map.querySelectorAll('canvas,foreignObject').length,
      geometryFits:Array.from(map.querySelectorAll('.m3-node,.m3-link-label,path,polygon'))
        .filter(visible).every(element => inside(rect(element), rect(viewport))),
      textFits:nodes.every(node => {
        const range = document.createRange(); range.selectNodeContents(node.querySelector('.m3-topic'));
        return inside(range.getBoundingClientRect(), rect(node));
      }),
      styled:{fontSize:parseFloat(topicStyle.fontSize), weight:parseInt(topicStyle.fontWeight,10),
        italic:topicStyle.fontStyle, color:topicStyle.color,
        background:getComputedStyle(styledNode).backgroundColor},
      breakAfter:getComputedStyle(visual).breakAfter,
      pageBreakAfter:getComputedStyle(visual).pageBreakAfter,
      chromeHidden:Array.from(document.querySelectorAll('main > h1, #visual-map > h2, #visual-map > p'))
        .every(element => !visible(element)),
      headings:entries.map(entry => entry.querySelector('h3').innerText.replace(/\s+/g, ' ').trim()),
      articleVisibility:entries.map(visible),
      text:articles.innerText,
      links:Array.from(articles.querySelectorAll('a'), link => ({href:link.getAttribute('href'),
        targetExists:!link.hash || !!document.getElementById(decodeURIComponent(link.hash.slice(1)))})),
      deepHeading:rect(deep.querySelector('h3')),
      rootHeading:rect(entries[0].querySelector('h3')),
      note:rect(note), noteBreak:getComputedStyle(note).breakInside,
      noteFont:parseFloat(getComputedStyle(note).fontSize),
      headingBreak:getComputedStyle(deep.querySelector('h3')).breakAfter
    });
  })())JS"));
  return QJsonDocument::fromJson(result.toString().toUtf8()).object();
}

void verifyCompleteArticles(const QJsonObject &p_state) {
  const auto headings = p_state.value(QStringLiteral("headings")).toArray();
  QCOMPARE(headings.size(), topics().size());
  for (const auto &topic : topics()) {
    QVERIFY2(headings.contains(topic.simplified()), qPrintable(topic));
  }
  QVERIFY(p_state.value(QStringLiteral("articlesVisible")).toBool());
  for (const auto &visible : p_state.value(QStringLiteral("articleVisibility")).toArray()) {
    QVERIFY(visible.toBool());
  }
  const auto text = p_state.value(QStringLiteral("text")).toString().simplified();
  QVERIFY(text.contains(longNote().simplified()));
  QVERIFY(text.contains(QStringLiteral("Across the forest")));
  bool externalLink = false;
  int internalLinks = 0;
  for (const auto &value : p_state.value(QStringLiteral("links")).toArray()) {
    const auto link = value.toObject();
    const auto href = link.value(QStringLiteral("href")).toString();
    if (href.startsWith(QLatin1Char('#'))) {
      ++internalLinks;
      QVERIFY(link.value(QStringLiteral("targetExists")).toBool());
    }
    externalLink |= href == QStringLiteral("https://example.invalid/deep?q=1&x=2");
  }
  QVERIFY(externalLink);
  QVERIFY(internalLinks >= 2);
}

void verifyMapDocument(const QJsonObject &p_state) {
  const auto exportedTopics = p_state.value(QStringLiteral("topics")).toArray();
  QCOMPARE(exportedTopics.size(), topics().size());
  for (const auto &topic : topics()) {
    QVERIFY2(exportedTopics.contains(topic.simplified()), qPrintable(topic));
  }
  QCOMPARE(p_state.value(QStringLiteral("svgNamespace")).toString(),
           QStringLiteral("http://www.w3.org/2000/svg"));
  QCOMPARE(p_state.value(QStringLiteral("treeEdges")).toInt(), topics().size() - 2);
  QCOMPARE(p_state.value(QStringLiteral("crossLinks")).toInt(), 1);
  QCOMPARE(p_state.value(QStringLiteral("rasterSurfaces")).toInt(), 0);
  QCOMPARE(p_state.value(QStringLiteral("svgPointerEvents")).toString(), QStringLiteral("none"));
  QVERIFY(p_state.value(QStringLiteral("topicUserSelect")).toString() != QStringLiteral("none"));
  QCOMPARE(p_state.value(QStringLiteral("rtlDirection")).toString(), QStringLiteral("rtl"));
  QVERIFY2(p_state.value(QStringLiteral("geometryFits")).toBool(),
           "All nodes, connectors, arrows and labels must fit the exported viewport");
  QVERIFY2(p_state.value(QStringLiteral("textFits")).toBool(),
           "Browser-rendered topic glyphs must remain inside their measured node boxes");
  const auto styled = p_state.value(QStringLiteral("styled")).toObject();
  QCOMPARE(styled.value(QStringLiteral("fontSize")).toDouble(), 23.0);
  QVERIFY(styled.value(QStringLiteral("weight")).toInt() >= 600);
  QCOMPARE(styled.value(QStringLiteral("italic")).toString(), QStringLiteral("italic"));
  QCOMPARE(styled.value(QStringLiteral("color")).toString(), QStringLiteral("rgb(87, 41, 165)"));
  QCOMPARE(styled.value(QStringLiteral("background")).toString(),
           QStringLiteral("rgb(252, 232, 184)"));

  QVERIFY(p_state.value(QStringLiteral("decoded")).toBool());
  const auto source = p_state.value(QStringLiteral("source")).toString();
  const auto prefix = QStringLiteral("data:image/png;base64,");
  QVERIFY(source.startsWith(prefix));
  const auto image =
      QImage::fromData(QByteArray::fromBase64(source.mid(prefix.size()).toLatin1()), "PNG");
  QCOMPARE(image.size(), QSize(123, 67));
  QCOMPARE(image.pixelColor(0, 0), QColor(241, 11, 211));
  QCOMPARE(image.pixelColor(61, 33), QColor(7, 223, 191));
}

void verifyPdf(const QString &p_path) {
  const auto bytes = readFile(p_path);
  QVERIFY(bytes.startsWith("%PDF-"));
  QVERIFY(bytes.trimmed().endsWith("%%EOF"));
  QVERIFY(QRegularExpression(QStringLiteral("/Type\\s*/Page\\b"))
              .match(QString::fromLatin1(bytes))
              .hasMatch());
}

bool pdfContainsFixtureImage(const QByteArray &p_pdf) {
  const auto text = QString::fromLatin1(p_pdf);
  const QRegularExpression dictionaries(QStringLiteral("<<[^<>]*>>"));
  auto matches = dictionaries.globalMatch(text);
  while (matches.hasNext()) {
    const auto dictionary = matches.next().captured();
    if (QRegularExpression(QStringLiteral("/Subtype\\s*/Image\\b")).match(dictionary).hasMatch() &&
        QRegularExpression(QStringLiteral("/Width\\s+123\\b")).match(dictionary).hasMatch() &&
        QRegularExpression(QStringLiteral("/Height\\s+67\\b")).match(dictionary).hasMatch()) {
      return true;
    }
  }
  return false;
}

// The held image endpoint synchronizes cancellation with a real pending Chromium request.
// Fixed in-memory resources also serve the bundled PDF reader without public-network access.
class HeldImageServer : public QObject {
  Q_OBJECT
public:
  HeldImageServer() {
    connect(&m_server, &QTcpServer::newConnection, this, [this] {
      while (m_server.hasPendingConnections()) {
        auto *socket = m_server.nextPendingConnection();
        socket->setParent(this);
        const auto request = std::make_shared<QByteArray>();
        const auto handled = std::make_shared<bool>(false);
        connect(socket, &QTcpSocket::readyRead, this,
                [this, socket = QPointer<QTcpSocket>(socket), request, handled] {
                  if (!socket || *handled) {
                    return;
                  }
                  *request += socket->readAll();
                  if (!request->contains("\r\n\r\n")) {
                    return;
                  }
                  *handled = true;
                  if (request->startsWith("GET /held.png ")) {
                    m_held = socket;
                    ++m_requests;
                    emit imageRequested();
                  } else {
                    const auto resource = m_resources.constFind(request->split(' ').value(1));
                    if (resource == m_resources.cend()) {
                      socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: "
                                    "close\r\n\r\n");
                    } else {
                      socket->write(
                          "HTTP/1.1 200 OK\r\nContent-Type: " + resource->mime +
                          "\r\nContent-Length: " + QByteArray::number(resource->content.size()) +
                          "\r\nConnection: close\r\n\r\n" + resource->content);
                    }
                    socket->disconnectFromHost();
                  }
                });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
      }
    });
  }

  bool listen() { return m_server.listen(QHostAddress::LocalHost, 0); }
  void addResource(const QByteArray &p_path, const QByteArray &p_mime,
                   const QByteArray &p_content) {
    m_resources.insert(p_path, {p_mime, p_content});
  }
  QString imageUrl() const {
    return QStringLiteral("http://127.0.0.1:%1/held.png").arg(m_server.serverPort());
  }
  int requests() const { return m_requests; }
  bool released() const { return m_released; }
  bool settled() const { return !m_held || m_held->state() == QAbstractSocket::UnconnectedState; }

  void release() {
    m_released = true;
    if (!m_held || m_held->state() == QAbstractSocket::UnconnectedState) {
      return;
    }
    const auto bytes = imageBytes();
    m_held->write("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nCache-Control: no-store\r\n"
                  "Content-Length: " +
                  QByteArray::number(bytes.size()) + "\r\nConnection: close\r\n\r\n" + bytes);
    m_held->disconnectFromHost();
  }

signals:
  void imageRequested();

private:
  struct Resource {
    QByteArray mime, content;
  };
  QHash<QByteArray, Resource> m_resources;
  QTcpServer m_server;
  QPointer<QTcpSocket> m_held;
  int m_requests = 0;
  bool m_released = false;
};

#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
// Use the application's bundled PDF reader (Chrome 125+), not PDF syntax/font heuristics.
// The fixed loopback resources add no test dependency and never reach the public network.
QJsonArray readPdfText(const QString &p_file) {
  const auto module = readFile(QFINDTESTDATA("../../src/data/extra/web/pdf.js/build/pdf.mjs"));
  const auto worker =
      readFile(QFINDTESTDATA("../../src/data/extra/web/pdf.js/build/pdf.worker.mjs"));
  if (module.isEmpty() || worker.isEmpty()) {
    qWarning() << "Bundled PDF reader assets are unavailable";
    return {};
  }
  HeldImageServer server;
  if (!server.listen()) {
    return {};
  }
  server.addResource("/pdf.mjs", "text/javascript", module);
  server.addResource("/pdf.worker.mjs", "text/javascript", worker);
  server.addResource("/document.pdf", "application/pdf", readFile(p_file));
  server.addResource("/reader.html", "text/html; charset=utf-8", QByteArrayLiteral(R"HTML(
<!doctype html><meta charset="utf-8"><pre id="result" data-state="loading"></pre>
<script type="module">
import * as pdfjs from '/pdf.mjs';
pdfjs.GlobalWorkerOptions.workerSrc = '/pdf.worker.mjs';
const result = document.getElementById('result');
try {
  const pdf = await pdfjs.getDocument({url:'/document.pdf'}).promise;
  const pages = [];
  for (let number = 1; number <= Math.min(2, pdf.numPages); ++number) {
    const content = await (await pdf.getPage(number)).getTextContent();
    pages.push(content.items.map(item => ({text:item.str, height:item.height})));
  }
  result.textContent = JSON.stringify(pages);
  result.dataset.state = 'ready';
} catch (error) {
  result.textContent = String(error);
  result.dataset.state = 'error';
}
</script>)HTML"));
  BrowserPage browser;
  QSignalSpy loaded(&browser.page, &QWebEnginePage::loadFinished);
  QUrl url(server.imageUrl());
  url.setPath(QStringLiteral("/reader.html"));
  browser.page.load(url);
  if (!QTest::qWaitFor([&loaded] { return !loaded.isEmpty(); }, 15000) ||
      !loaded.last().first().toBool()) {
    return {};
  }
  if (!QTest::qWaitFor(
          [&browser] {
            const auto state =
                evaluateJavaScript(
                    browser.page, QStringLiteral("document.getElementById('result').dataset.state"))
                    .toString();
            return state == QStringLiteral("ready") || state == QStringLiteral("error");
          },
          30000)) {
    return {};
  }
  const auto result =
      evaluateJavaScript(browser.page,
                         QStringLiteral("document.getElementById('result').textContent"))
          .toString();
  const auto document = QJsonDocument::fromJson(result.toUtf8());
  if (!document.isArray()) {
    qWarning().noquote() << "PDF text extraction failed:" << result;
  }
  return document.array();
}
#endif

} // namespace

class TestHtmlPdfExporter : public QObject {
  Q_OBJECT
private slots:
  void standaloneToggleAndCompleteNativeForest();
  void printableNativeForest_data();
  void printableNativeForest();
  void customShowsBothViewsWithoutPrintGeometry();
  void largeHtmlAndLocalSidecarProducePdf();
  void invalidInputsLeaveNoOutput();
  void delayedImageSettlesBeforePdf();
  void cancellationDiscardsLateResponseAndConverterCanBeReused();
};

void TestHtmlPdfExporter::standaloneToggleAndCompleteNativeForest() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  QVERIFY(writeFile(directory.filePath(QStringLiteral("fixture.png")), imageBytes()));
  ServiceLocator services;
  MindMapExporter renderer(services, nullptr, nullptr);
  const auto html = directory.filePath(QStringLiteral("standalone.html"));
  QVERIFY(renderer.renderHtml(nativeInput(directory.path()), ExportHtmlRequest(), html));

  BrowserPage browser;
  QVERIFY(browser.profile.isOffTheRecord());
  QVERIFY(loadFile(browser.page, html));
  auto state = documentState(browser.page);
  QVERIFY(!state.isEmpty());
  QVERIFY(state.value(QStringLiteral("mapVisible")).toBool());
  QVERIFY(!state.value(QStringLiteral("articlesVisible")).toBool());
  QVERIFY(state.value(QStringLiteral("toggleVisible")).toBool());
  verifyMapDocument(state);
  const auto rootTopic = state.value(QStringLiteral("rootTopic")).toObject();
  const QPoint wordPoint(qRound(rootTopic.value(QStringLiteral("left")).toDouble() + 4),
                         qRound(rootTopic.value(QStringLiteral("top")).toDouble() + 7));
  QVERIFY2(
      evaluateJavaScript(browser.page,
                         QStringLiteral("!!document.elementFromPoint(%1,%2).closest('.m3-topic')")
                             .arg(wordPoint.x())
                             .arg(wordPoint.y()))
          .toBool(),
      "The SVG connector layer must not intercept node text pointer hits");
  const auto selected = evaluateJavaScript(browser.page, QStringLiteral(R"JS((() => {
    const range = document.createRange();
    range.selectNodeContents(document.querySelector('#mindmap .m3-topic'));
    const selection = window.getSelection();
    selection.removeAllRanges();
    selection.addRange(range);
    return selection.toString();
  })())JS"))
                            .toString();
  QCOMPARE(selected.simplified(), topics().first().simplified());

  const auto originalWidth =
      state.value(QStringLiteral("map")).toObject().value(QStringLiteral("width")).toDouble();
  browser.view.resize(640, 900);
  QTRY_VERIFY_WITH_TIMEOUT(documentState(browser.page)
                                   .value(QStringLiteral("map"))
                                   .toObject()
                                   .value(QStringLiteral("width"))
                                   .toDouble() < originalWidth,
                           5000);
  state = documentState(browser.page);
  verifyMapDocument(state);
  const auto fitted = state.value(QStringLiteral("map")).toObject();
  const auto viewport = state.value(QStringLiteral("mapViewport")).toObject();
  QVERIFY(std::abs(fitted.value(QStringLiteral("width")).toDouble() -
                   viewport.value(QStringLiteral("width")).toDouble()) < 2);
  QVERIFY(std::abs(fitted.value(QStringLiteral("height")).toDouble() -
                   viewport.value(QStringLiteral("height")).toDouble()) < 2);

  QVERIFY(evaluateJavaScript(browser.page,
                             QStringLiteral("document.getElementById('view-toggle').click(); true"))
              .toBool());
  state = documentState(browser.page);
  QVERIFY(!state.value(QStringLiteral("mapVisible")).toBool());
  verifyCompleteArticles(state);

  QVERIFY(evaluateJavaScript(browser.page,
                             QStringLiteral("document.getElementById('view-toggle').click(); true"))
              .toBool());
  state = documentState(browser.page);
  QVERIFY(state.value(QStringLiteral("mapVisible")).toBool());
  QVERIFY(!state.value(QStringLiteral("articlesVisible")).toBool());
}

void TestHtmlPdfExporter::printableNativeForest_data() {
  QTest::addColumn<bool>("landscape");
  QTest::newRow("portrait") << false;
  QTest::newRow("landscape") << true;
}

void TestHtmlPdfExporter::printableNativeForest() {
  QFETCH(bool, landscape);
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  QVERIFY(writeFile(directory.filePath(QStringLiteral("fixture.png")), imageBytes()));
  ServiceLocator services;
  MindMapExporter renderer(services, nullptr, nullptr);
  ExportHtmlRequest request;
  request.purpose = ExportHtmlPurpose::Pdf;
  request.options.m_pdfOption =
      pdfOption(landscape ? QPageLayout::Landscape : QPageLayout::Portrait);
  const auto html = directory.filePath(QStringLiteral("print.html"));
  QVERIFY(renderer.renderHtml(nativeInput(directory.path()), request, html));

  const auto printable = request.options.m_pdfOption.m_layout->paintRect(QPageLayout::Millimeter);
  constexpr double pixelsPerMillimeter = 96.0 / 25.4;
  BrowserPage browser;
  browser.view.resize(static_cast<int>(std::ceil(printable.width() * pixelsPerMillimeter)), 900);
  QVERIFY(loadFile(browser.page, html));
  const auto state = documentState(browser.page);
  QVERIFY(!state.isEmpty());
  verifyMapDocument(state);
  verifyCompleteArticles(state);
  QVERIFY(state.value(QStringLiteral("mapVisible")).toBool());
  QVERIFY(!state.value(QStringLiteral("toggleVisible")).toBool());
  QVERIFY(state.value(QStringLiteral("chromeHidden")).toBool());
  const auto map = state.value(QStringLiteral("map")).toObject();
  const auto viewport = state.value(QStringLiteral("mapViewport")).toObject();
  const auto visual = state.value(QStringLiteral("visual")).toObject();
  const auto articles = state.value(QStringLiteral("articles")).toObject();
  const double width = map.value(QStringLiteral("width")).toDouble();
  const double height = map.value(QStringLiteral("height")).toDouble();
  QVERIFY(width > 0 && height > 0);
  QVERIFY(width <= (printable.width() - 1) * pixelsPerMillimeter + 1);
  QVERIFY(height <= (printable.height() - 1) * pixelsPerMillimeter + 1);
  QVERIFY(std::abs(width - viewport.value(QStringLiteral("width")).toDouble()) < 1);
  QVERIFY(std::abs(height - viewport.value(QStringLiteral("height")).toDouble()) < 1);
  QVERIFY(std::abs(map.value(QStringLiteral("left")).toDouble()) < 1);
  QVERIFY(std::abs(map.value(QStringLiteral("top")).toDouble()) < 1);
  QVERIFY(visual.value(QStringLiteral("bottom")).toDouble() <=
          printable.height() * pixelsPerMillimeter);
  const double nativeRatio = state.value(QStringLiteral("naturalWidth")).toDouble() /
                             state.value(QStringLiteral("naturalHeight")).toDouble();
  QVERIFY(std::abs(width / height - nativeRatio) / nativeRatio < 0.005);
  QVERIFY(articles.value(QStringLiteral("top")).toDouble() >=
          visual.value(QStringLiteral("bottom")).toDouble());
  QVERIFY(state.value(QStringLiteral("breakAfter")).toString() == QStringLiteral("page") ||
          state.value(QStringLiteral("pageBreakAfter")).toString() == QStringLiteral("always"));
  const auto deep = state.value(QStringLiteral("deepHeading")).toObject();
  const auto root = state.value(QStringLiteral("rootHeading")).toObject();
  QVERIFY(std::abs(deep.value(QStringLiteral("left")).toDouble() -
                   root.value(QStringLiteral("left")).toDouble()) < 1);
  QVERIFY(deep.value(QStringLiteral("width")).toDouble() >
          printable.width() * pixelsPerMillimeter * 0.9);
  QVERIFY(
      state.value(QStringLiteral("note")).toObject().value(QStringLiteral("height")).toDouble() >
      printable.height() * pixelsPerMillimeter);
  QCOMPARE(state.value(QStringLiteral("noteBreak")).toString(), QStringLiteral("auto"));
  QVERIFY(state.value(QStringLiteral("noteFont")).toDouble() >= 12);
  QVERIFY(
      state.value(QStringLiteral("headingBreak")).toString().startsWith(QStringLiteral("avoid")));

  // Screen resize invokes the standalone fitter; it must not override the fixed print geometry.
  browser.view.resize(400, 700);
  QVERIFY(
      evaluateJavaScript(browser.page, QStringLiteral("dispatchEvent(new Event('resize')); true"))
          .toBool());
  const auto resized = documentState(browser.page);
  verifyMapDocument(resized);
  const auto resizedMap = resized.value(QStringLiteral("map")).toObject();
  QVERIFY(std::abs(resizedMap.value(QStringLiteral("width")).toDouble() - width) < 1);
  QVERIFY(std::abs(resizedMap.value(QStringLiteral("height")).toDouble() - height) < 1);

  HtmlPdfExporter converter(services);
  connect(&converter, &HtmlPdfExporter::logRequested, this,
          [](const QString &p_message) { qInfo().noquote() << p_message; });
  const auto pdf = directory.filePath(QStringLiteral("forest.pdf"));
  QVERIFY(converter.exportFiles(request.options.m_pdfOption, {html}, pdf));
  verifyPdf(pdf);
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
  const auto pages = readPdfText(pdf);
  QVERIFY2(pages.size() == 2, "The actual PDF reader must expose the map and first article page");
  QString firstPage;
  for (const auto &item : pages[0].toArray()) {
    firstPage += item.toObject().value(QStringLiteral("text")).toString() + QLatin1Char(' ');
  }
  QVERIFY(firstPage.contains(QStringLiteral("Root")));
  QVERIFY(firstPage.contains(QStringLiteral("Level seven")));
  QVERIFY(firstPage.contains(QStringLiteral("Separate orphan tree")));
  bool readableBody = false;
  for (const auto &item : pages[1].toArray()) {
    const auto text = item.toObject().value(QStringLiteral("text")).toString();
    if (text.contains(QStringLiteral("Paragraph 0:"))) {
      const auto height = item.toObject().value(QStringLiteral("height")).toDouble();
      QVERIFY2(
          std::abs(height - 12.0) < 0.1,
          qPrintable(QStringLiteral("Diagram overflow shrank PDF body text to %1 pt").arg(height)));
      readableBody = true;
    }
  }
  QVERIFY(readableBody);
#endif
}

void TestHtmlPdfExporter::customShowsBothViewsWithoutPrintGeometry() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  QVERIFY(writeFile(directory.filePath(QStringLiteral("fixture.png")), imageBytes()));
  ServiceLocator services;
  MindMapExporter renderer(services, nullptr, nullptr);
  ExportHtmlRequest request;
  request.purpose = ExportHtmlPurpose::Custom;
  // Custom is not a paper-based format: it must work even without a page layout.
  request.options.m_pdfOption.m_layout.clear();
  const auto html = directory.filePath(QStringLiteral("custom.html"));
  QVERIFY(renderer.renderHtml(nativeInput(directory.path()), request, html));
  BrowserPage browser;
  QVERIFY(loadFile(browser.page, html));
  const auto state = documentState(browser.page);
  QVERIFY(!state.isEmpty());
  verifyMapDocument(state);
  verifyCompleteArticles(state);
  QVERIFY(state.value(QStringLiteral("mapVisible")).toBool());
  QVERIFY(!state.value(QStringLiteral("toggleVisible")).toBool());
  QVERIFY(!state.value(QStringLiteral("chromeHidden")).toBool());
  QCOMPARE(state.value(QStringLiteral("breakAfter")).toString(), QStringLiteral("auto"));
  QCOMPARE(state.value(QStringLiteral("pageBreakAfter")).toString(), QStringLiteral("auto"));
}

void TestHtmlPdfExporter::largeHtmlAndLocalSidecarProducePdf() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  QVERIFY(QDir(directory.path()).mkpath(QStringLiteral("large_files")));
  QVERIFY(writeFile(directory.filePath(QStringLiteral("large_files/fixture.png")), imageBytes()));
  const auto html = directory.filePath(QStringLiteral("large.html"));
  QByteArray content(
      "<!doctype html><meta charset='utf-8'><title>Large HTML transport</title><!--");
  content += QByteArray(2 * 1024 * 1024 + 4096, 'x');
  content += "--><h1>Content beyond the data URL limit</h1>"
             "<img id='sidecar' src='large_files/fixture.png' width='123' height='67'>";
  QVERIFY(writeFile(html, content));
  QVERIFY(QFileInfo(html).size() > 2 * 1024 * 1024);
  BrowserPage browser;
  QVERIFY(loadFile(browser.page, html));
  QVERIFY(evaluateJavaScript(browser.page, QStringLiteral(R"JS((() => {
    const image = document.getElementById('sidecar');
    return image.complete && image.naturalWidth === 123 && image.naturalHeight === 67 &&
      document.body.innerText.includes('Content beyond the data URL limit');
  })())JS"))
              .toBool());

  ServiceLocator services;
  HtmlPdfExporter converter(services);
  connect(&converter, &HtmlPdfExporter::logRequested, this,
          [](const QString &p_message) { qInfo().noquote() << p_message; });
  const auto pdf = directory.filePath(QStringLiteral("large.pdf"));
  QVERIFY(converter.exportFiles(pdfOption(), {html}, pdf));
  verifyPdf(pdf);
  QVERIFY2(pdfContainsFixtureImage(readFile(pdf)),
           "The completed PDF must embed the local sidecar image at its decoded dimensions");
}

void TestHtmlPdfExporter::invalidInputsLeaveNoOutput() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  ServiceLocator services;
  HtmlPdfExporter converter(services);
  connect(&converter, &HtmlPdfExporter::logRequested, this,
          [](const QString &p_message) { qInfo().noquote() << p_message; });
  QSignalSpy logs(&converter, &HtmlPdfExporter::logRequested);
  const auto missing = directory.filePath(QStringLiteral("missing.html"));
  const auto output = directory.filePath(QStringLiteral("missing.pdf"));
  QVERIFY(!converter.exportFiles(pdfOption(), {missing}, output));
  QVERIFY(!QFileInfo::exists(output));
  QVERIFY(!logs.isEmpty());
  QVERIFY(!converter.exportFiles(pdfOption(), {}, output));
  QVERIFY(!QFileInfo::exists(output));
  const auto valid = directory.filePath(QStringLiteral("valid.html"));
  QVERIFY(writeFile(valid, "<!doctype html><h1>Readable document</h1>"));
  QVERIFY(!converter.exportFiles(pdfOption(), {valid, valid}, output));
  QVERIFY(!QFileInfo::exists(output));
}

void TestHtmlPdfExporter::delayedImageSettlesBeforePdf() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  HeldImageServer server;
  QVERIFY(server.listen());
  const auto html = directory.filePath(QStringLiteral("delayed.html"));
  // Start the request from load, so readiness must also settle images which did not hold up
  // the original document's load event.
  QVERIFY(writeFile(html, QStringLiteral("<!doctype html><h1>Delayed resource</h1><img id='late'>"
                                         "<script>addEventListener('load', () => "
                                         "{document.getElementById('late').src='%1';});</script>")
                              .arg(server.imageUrl())
                              .toUtf8()));
  const auto serverPointer = QPointer<HeldImageServer>(&server);
  connect(&server, &HeldImageServer::imageRequested, &server, [serverPointer] {
    QTimer::singleShot(0, serverPointer.data(), [serverPointer] {
      if (serverPointer) {
        serverPointer->release();
      }
    });
  });
  ServiceLocator services;
  HtmlPdfExporter converter(services);
  connect(&converter, &HtmlPdfExporter::logRequested, this,
          [](const QString &p_message) { qInfo().noquote() << p_message; });
  const auto pdf = directory.filePath(QStringLiteral("delayed.pdf"));
  QVERIFY(converter.exportFiles(pdfOption(), {html}, pdf));
  QCOMPARE(server.requests(), 1);
  QVERIFY(server.released());
  verifyPdf(pdf);
  QVERIFY2(pdfContainsFixtureImage(readFile(pdf)),
           "Printing must wait for the released image rather than snapshot a pending resource");
}

void TestHtmlPdfExporter::cancellationDiscardsLateResponseAndConverterCanBeReused() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  HeldImageServer server;
  QVERIFY(server.listen());
  const auto html = directory.filePath(QStringLiteral("cancelled.html"));
  QVERIFY(writeFile(html, QStringLiteral("<!doctype html><h1>Cancelled document</h1><img id='late'>"
                                         "<script>addEventListener('load', () => "
                                         "{document.getElementById('late').src='%1';});</script>")
                              .arg(server.imageUrl())
                              .toUtf8()));
  ServiceLocator services;
  HtmlPdfExporter converter(services);
  connect(&converter, &HtmlPdfExporter::logRequested, this,
          [](const QString &p_message) { qInfo().noquote() << p_message; });
  const QPointer<HtmlPdfExporter> converterPointer(&converter);
  const auto stopped = std::make_shared<bool>(false);
  const auto connection =
      connect(&server, &HeldImageServer::imageRequested, &converter, [converterPointer, stopped] {
        *stopped = true;
        if (converterPointer) {
          converterPointer->stop();
        }
      });
  const auto output = directory.filePath(QStringLiteral("cancelled.pdf"));
  QVERIFY(!converter.exportFiles(pdfOption(), {html}, output));
  QCOMPARE(server.requests(), 1);
  QVERIFY(*stopped);
  QVERIFY(!QFileInfo::exists(output));
  disconnect(connection);

  // Release only after exportFiles() has returned. Its callbacks can now arrive while the same
  // converter is handling a different document, or observe the cancellation's closed socket.
  const auto serverPointer = QPointer<HeldImageServer>(&server);
  QTimer::singleShot(0, &server, [serverPointer] {
    if (serverPointer) {
      serverPointer->release();
    }
  });
  const auto replacement = directory.filePath(QStringLiteral("replacement.html"));
  const auto replacementPdf = directory.filePath(QStringLiteral("replacement.pdf"));
  QVERIFY(
      writeFile(replacement, "<!doctype html><h1>Successful operation after cancellation</h1>"));
  QVERIFY(converter.exportFiles(pdfOption(), {replacement}, replacementPdf));
  verifyPdf(replacementPdf);
  QVERIFY(server.released());
  QVERIFY(QTest::qWaitFor([serverPointer] { return serverPointer && serverPointer->settled(); },
                          10000));
  QVERIFY(!QFileInfo::exists(output));
}

} // namespace tests

int main(int argc, char **argv) {
  vxcore_set_test_mode(1);
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QStandardPaths::setTestModeEnabled(true);
#ifdef Q_OS_WIN
  if (qgetenv("QT_QPA_PLATFORM") == "offscreen" && qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR")) {
    qputenv("QT_QPA_FONTDIR", qgetenv("WINDIR") + "/Fonts");
  }
#endif
  QApplication application(argc, argv);
  application.setOrganizationName(QStringLiteral("VNoteHtmlPdfTest-") +
                                  QUuid::createUuid().toString(QUuid::WithoutBraces));
  application.setApplicationName(QStringLiteral("VNote"));
  tests::TestHtmlPdfExporter test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_htmlpdfexporter.moc"
