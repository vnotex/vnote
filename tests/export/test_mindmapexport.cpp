// SPDX-License-Identifier: LGPL-3.0-or-later

#include <QtTest>

#include <QApplication>
#include <QBuffer>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QPageLayout>
#include <QPageSize>
#include <QPointer>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <QTimer>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineView>
#include <QXmlStreamReader>

#include <memory>

#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/mindmapeditorconfig.h>
#include <core/servicelocator.h>
#include <core/services/configcoreservice.h>
#include <export/exporter.h>
#include <export/exportrendererfactory.h>
#include <export/mindmapexporter.h>
#include <utils/markdownhtmlrenderer.h>
#include <vxcore/vxcore.h>

using namespace vnotex;

namespace {

const QColor c_localColor(233, 19, 147);
const QColor c_embeddedColor(17, 231, 91);
const QColor c_secondColor(47, 61, 239);

bool writeBytes(const QString &p_path, const QByteArray &p_bytes) {
  if (!QDir().mkpath(QFileInfo(p_path).absolutePath())) {
    return false;
  }
  QFile file(p_path);
  return file.open(QIODevice::WriteOnly) && file.write(p_bytes) == p_bytes.size();
}

QByteArray readBytes(const QString &p_path) {
  QFile file(p_path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QByteArray imageBytes(const QColor &p_color) {
  QImage image(48, 32, QImage::Format_RGB32);
  image.fill(p_color);
  QByteArray bytes;
  QBuffer buffer(&bytes);
  if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
    return {};
  }
  return bytes;
}

QJsonObject imageObject(const QString &p_url) {
  return {{QStringLiteral("url"), p_url},
          {QStringLiteral("width"), 48},
          {QStringLiteral("height"), 32}};
}

QString simpleMap(const QString &p_topic, const QString &p_image = QString()) {
  QJsonObject root{{QStringLiteral("id"), QStringLiteral("root")},
                   {QStringLiteral("topic"), p_topic}};
  if (!p_image.isEmpty()) {
    root.insert(QStringLiteral("image"), imageObject(p_image));
  }
  return QString::fromUtf8(
      QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"), 1},
                                {QStringLiteral("rootId"), QStringLiteral("root")},
                                {QStringLiteral("nodes"), QJsonArray{root}},
                                {QStringLiteral("crossLinks"), QJsonArray{}}})
          .toJson(QJsonDocument::Compact));
}

QStringList forestTopics() {
  return {QStringLiteral("Racine \u4e16\u754c \u03a9 <&>"),
          QStringLiteral("Second level caf\u00e9"),
          QStringLiteral("Third level"),
          QStringLiteral("Fourth level"),
          QStringLiteral("Fifth level \u6df1\u5c42"),
          QStringLiteral("Orphan \u72ec\u7acb"),
          QStringLiteral("Orphan child")};
}

QString longNote() {
  return QStringLiteral("Long note starts \u00e9\u6ce8\u91c8\n") +
         QStringLiteral("Readable detailed paragraph, not native JSON.\n").repeated(45) +
         QStringLiteral("Long note ends here");
}

QString forest(const QString &p_localImage = QStringLiteral("image.png"),
               const QString &p_embeddedImage = QString()) {
  const auto topics = forestTopics();
  const QStringList ids{QStringLiteral("root"),        QStringLiteral("second"),
                        QStringLiteral("third"),       QStringLiteral("fourth"),
                        QStringLiteral("fifth"),       QStringLiteral("orphan"),
                        QStringLiteral("orphan-child")};
  QJsonArray nodes;
  for (int i = 0; i < ids.size(); ++i) {
    QJsonObject node{{QStringLiteral("id"), ids[i]},
                     {QStringLiteral("topic"), topics[i]},
                     {QStringLiteral("expanded"), false}};
    if (i < 4 || i == 5) {
      node.insert(QStringLiteral("children"), QJsonArray{ids[i + 1]});
    }
    if (i == 0 && !p_localImage.isEmpty()) {
      node.insert(QStringLiteral("image"), imageObject(p_localImage));
    }
    if (i == 4) {
      node.insert(QStringLiteral("note"), longNote());
      node.insert(QStringLiteral("hyperLink"), QStringLiteral("https://example.test/deep"));
    }
    if (i == 6 && !p_embeddedImage.isEmpty()) {
      node.insert(QStringLiteral("image"), imageObject(p_embeddedImage));
    }
    nodes.append(node);
  }
  const QJsonObject link{{QStringLiteral("id"), QStringLiteral("forest-link")},
                         {QStringLiteral("source"), QStringLiteral("fifth")},
                         {QStringLiteral("target"), QStringLiteral("orphan-child")},
                         {QStringLiteral("topic"), QStringLiteral("Across the forest")},
                         {QStringLiteral("directed"), true}};
  return QString::fromUtf8(
      QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"), 1},
                                {QStringLiteral("rootId"), ids.first()},
                                {QStringLiteral("orphanIds"), QJsonArray{ids[5]}},
                                {QStringLiteral("nodes"), nodes},
                                {QStringLiteral("crossLinks"), QJsonArray{link}}})
          .toJson(QJsonDocument::Compact));
}

bool containsForest(const QString &p_text, bool p_markdown = false) {
  for (const auto &topic : forestTopics()) {
    if (!p_text.contains(p_markdown ? topic.toHtmlEscaped() : topic)) {
      return false;
    }
  }
  return p_text.contains(QStringLiteral("Long note starts \u00e9\u6ce8\u91c8")) &&
         p_text.contains(QStringLiteral("Long note ends here")) &&
         p_text.contains(QStringLiteral("https://example.test/deep")) &&
         (!p_markdown || p_text.contains(QStringLiteral("Across the forest")));
}

QString htmlText(const QString &p_html) {
  QTextDocument document;
  document.setHtml(p_html);
  return document.toPlainText();
}

QVector<QImage> embeddedNodeImages(
    const QString &p_html, const QString &p_class = QStringLiteral("m3-node-image")) {
  QVector<QImage> images;
  QXmlStreamReader reader(p_html);
  while (!reader.atEnd()) {
    if (reader.readNext() != QXmlStreamReader::StartElement ||
        reader.name() != QLatin1String("img")) {
      continue;
    }
    const auto attributes = reader.attributes();
    if (!attributes.value(QStringLiteral("class"))
             .toString()
             .split(QLatin1Char(' '))
             .contains(p_class)) {
      continue;
    }
    const auto source = attributes.value(QStringLiteral("src")).toString();
    const auto prefix = QStringLiteral("data:image/png;base64,");
    images.append(source.startsWith(prefix) ? QImage::fromData(QByteArray::fromBase64(
                                                  source.mid(prefix.size()).toLatin1()))
                                            : QImage());
  }
  return images;
}

int imageStateCount(const QString &p_html, const QString &p_state) {
  QXmlStreamReader reader(p_html);
  int count = 0;
  while (!reader.atEnd()) {
    if (reader.readNext() == QXmlStreamReader::StartElement &&
        reader.attributes().value(QStringLiteral("data-image-state")) == p_state) {
      ++count;
    }
  }
  return count;
}

int colorPixels(const QImage &p_image, const QColor &p_color) {
  const auto image = p_image.convertToFormat(QImage::Format_ARGB32);
  int count = 0;
  for (int y = 0; y < image.height(); ++y) {
    const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
    for (int x = 0; x < image.width(); ++x) {
      if (qAbs(qRed(line[x]) - p_color.red()) <= 3 &&
          qAbs(qGreen(line[x]) - p_color.green()) <= 3 &&
          qAbs(qBlue(line[x]) - p_color.blue()) <= 3 && qAlpha(line[x]) > 240) {
        ++count;
      }
    }
  }
  return count;
}

bool containsImageColor(const QString &p_html, const QColor &p_color) {
  const auto images = embeddedNodeImages(p_html);
  for (const auto &image : images) {
    if (colorPixels(image, p_color) >= 64) {
      return true;
    }
  }
  return false;
}

QStringList outputFiles(const QString &p_directory) {
  QStringList files;
  QDirIterator it(p_directory, QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    files.append(it.next());
  }
  return files;
}

ExportFileInfo source(const QString &p_path, const QString &p_type = QStringLiteral("MindMap")) {
  ExportFileInfo info;
  info.filePath = p_path;
  info.fileName = QFileInfo(p_path).fileName();
  info.resourcePath = QFileInfo(p_path).absolutePath();
  info.fileTypeName = p_type;
  return info;
}

ExportRendererFactory nativeRegistry(ServiceLocator &p_services) {
  ExportRendererFactory registry(p_services);
  const auto creator = [](ServiceLocator &p_locator, QWidget *p_host, QObject *p_parent) {
    return new MindMapExporter(p_locator, p_host, p_parent);
  };
  registry.registerRenderer(QStringLiteral("MindMap"), creator);
  registry.registerRenderer(QStringLiteral("NativeAlias"), creator);
  return registry;
}

ExportOption options(const QString &p_directory, ExportFormat p_format) {
  ExportOption option;
  option.m_targetFormat = p_format;
  option.m_outputDir = p_directory;
  option.m_exportAttachments = false;
  return option;
}

QString exportSingle(Exporter &p_exporter, const ExportOption &p_options,
                     const ExportFileInfo &p_source, const QString &p_snapshot) {
  return p_exporter.doExportFile(p_options, p_snapshot, p_source.filePath, p_source.fileName,
                                 p_source.resourcePath, p_source.attachmentFolderPath,
                                 p_source.fileTypeName);
}

bool loggedImageUrl(const QSignalSpy &p_logs, const QString &p_url) {
  for (const auto &entry : p_logs) {
    const auto message = entry.first().toString();
    if (message.contains(p_url)) {
      return true;
    }
  }
  return false;
}

// The complete request is held until the test explicitly releases it. All timers and socket
// callbacks are receiver-bound; callbacks never keep references to an export's stack flags.
class HeldImageServer : public QObject {
  Q_OBJECT
public:
  HeldImageServer() {
    connect(&m_server, &QTcpServer::newConnection, this, [this] {
      while (m_server.hasPendingConnections()) {
        auto *socket = m_server.nextPendingConnection();
        socket->setParent(this);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
          m_request += socket->readAll();
          if (!m_received && m_request.contains("\r\n\r\n")) {
            m_received = true;
            m_socket = socket;
            emit requestReceived();
          }
        });
      }
    });
  }

  bool listen() { return m_server.listen(QHostAddress::LocalHost, 0); }
  QString url() const {
    return QStringLiteral("http://127.0.0.1:%1/held.png").arg(m_server.serverPort());
  }
  void release(const QByteArray &p_body) {
    m_released = true;
    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
      const auto response = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\n") +
                            QByteArrayLiteral("Connection: close\r\nContent-Length: ") +
                            QByteArray::number(p_body.size()) + "\r\n\r\n" + p_body;
      m_socket->write(response);
      m_socket->disconnectFromHost();
    }
    emit responseReleased();
  }
  bool received() const { return m_received; }
  bool released() const { return m_released; }
  bool disconnected() const {
    return !m_socket || m_socket->state() == QAbstractSocket::UnconnectedState;
  }

signals:
  void requestReceived();
  void responseReleased();

private:
  QTcpServer m_server;
  QPointer<QTcpSocket> m_socket;
  QByteArray m_request;
  bool m_received = false;
  bool m_released = false;
};

class HtmlPage {
public:
  HtmlPage() : page(&profile) {
    page.settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
    view.setPage(&page);
    view.resize(1200, 900);
    view.show();
  }

  bool load(const QString &p_path) {
    QSignalSpy loaded(&page, &QWebEnginePage::loadFinished);
    page.load(QUrl::fromLocalFile(p_path));
    return QTest::qWaitFor([&loaded] { return !loaded.isEmpty(); }, 30000) &&
           loaded.last().first().toBool();
  }

  QVariant evaluate(const QString &p_script) {
    const auto state = std::make_shared<std::pair<bool, QVariant>>(false, QVariant());
    page.runJavaScript(p_script, [state](const QVariant &p_value) {
      state->second = p_value;
      state->first = true;
    });
    return QTest::qWaitFor([state] { return state->first; }, 10000) ? state->second : QVariant();
  }

  QVariantMap presentation() {
    return evaluate(QStringLiteral(R"JS((() => {
      const visible = selector => {
        const node = document.querySelector(selector);
        return !!node && getComputedStyle(node).display !== 'none' &&
               node.getBoundingClientRect().height > 0;
      };
      const images = Array.from(document.querySelectorAll('#mindmap .m3-node-image'));
      const articleImages = Array.from(document.querySelectorAll('#articles .m3-article-image'));
      return {map: visible('#mindmap'), articles: visible('#articles'),
              image: images.length > 0 && images.every(image => image.complete && image.naturalWidth > 0),
              articleImages: articleImages.length === images.length && articleImages.every(
                image => image.complete && image.naturalWidth > 0 &&
                         images.some(mapImage => mapImage.src === image.src)),
              topics: Array.from(document.querySelectorAll('#mindmap .m3-node .m3-topic'),
                                 topic => topic.textContent.replace(/\s+/g, ' ').trim()),
              articleTopics: Array.from(document.querySelectorAll('#articles .m3-article-topic'),
                                        topic => topic.textContent.replace(/\s+/g, ' ').trim()),
              treeEdges: document.querySelectorAll('.m3-connectors .m3-tree-edge').length,
              crossLinks: document.querySelectorAll('.m3-connectors .m3-cross-link').length,
              text: document.querySelector('#articles').textContent,
              links: Array.from(document.querySelectorAll('#articles a')).map(a => a.getAttribute('href'))};
    })())JS"))
        .toMap();
  }

  QWebEngineProfile profile;
  QWebEnginePage page;
  QWebEngineView view;
};

bool copyDirectory(const QString &p_source, const QString &p_destination) {
  if (!QDir().mkpath(p_destination)) {
    return false;
  }
  const auto entries = QDir(p_source).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot |
                                                    QDir::Hidden | QDir::System);
  for (const auto &entry : entries) {
    const auto destination = QDir(p_destination).filePath(entry.fileName());
    if (entry.isSymLink() ||
        (entry.isDir() ? !copyDirectory(entry.absoluteFilePath(), destination)
                       : !QFile::copy(entry.absoluteFilePath(), destination))) {
      return false;
    }
  }
  return true;
}

// This is only an external-format consumer: all inputs come from the real export pipeline.
// Retain copies, never the original intermediate directory, so assertions run after cleanup.
int captureExportInputs(const QStringList &p_arguments) {
  if (p_arguments.size() < 4) {
    return 2;
  }
  const auto destination = p_arguments.at(2);
  QTemporaryDir retained(QDir(QFileInfo(destination).absolutePath())
                             .filePath(QStringLiteral("captured-inputs-XXXXXX")));
  if (!retained.isValid()) {
    return 3;
  }
  QJsonArray inputs;
  for (int i = 3; i < p_arguments.size(); ++i) {
    const QFileInfo input(p_arguments.at(i));
    const auto directory = QDir(retained.path()).filePath(QString::number(i - 3));
    const auto copy = QDir(directory).filePath(input.fileName());
    if (!input.isFile() || !QDir().mkpath(directory) ||
        !QFile::copy(input.absoluteFilePath(), copy)) {
      return 4;
    }
    QJsonArray resources;
    const auto siblings = input.dir().entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto &sibling : siblings) {
      const auto resourceCopy = QDir(directory).filePath(sibling.fileName());
      if (!copyDirectory(sibling.absoluteFilePath(), resourceCopy)) {
        return 5;
      }
      resources.append(QJsonObject{{QStringLiteral("source"), sibling.absoluteFilePath()},
                                   {QStringLiteral("copy"), resourceCopy}});
    }
    inputs.append(QJsonObject{{QStringLiteral("source"), input.absoluteFilePath()},
                              {QStringLiteral("copy"), copy},
                              {QStringLiteral("resources"), resources}});
  }
  if (!writeBytes(destination, QJsonDocument(inputs).toJson())) {
    return 6;
  }
  retained.setAutoRemove(false);
  return 0;
}

} // namespace

class TestMindMapExport : public QObject {
  Q_OBJECT
private slots:
  void markdownProjectsWholeForestAndCopiesImage();
  void markdownDoesNotCreateWidgets();
  void markdownRendersDocumentHierarchy();
  void markdownLinksMatchHtmlDirection_data();
  void markdownLinksMatchHtmlDirection();
  void unknownAndEmptyTypesUseRegisteredFallback();
  void snapshotsAreAuthoritative_data();
  void snapshotsAreAuthoritative();
  void standaloneContainsBothImagesAndCompleteArticles();
  void sectionNumberingReflectsSettingsOnReuse_data();
  void sectionNumberingReflectsSettingsOnReuse();
  void batchRebasesSameRelativeImage();
  void malformedAndCollidingInputs_data();
  void malformedAndCollidingInputs();
  void delayedImageCompletesBeforeSnapshot();
  void failedImagesSettleWithPlaceholderAndWarning();
  void cancellationDoesNotPublishLateOutput();
  void directConsumerRequests_data();
  void directConsumerRequests();
  void qtPdfRunsThroughExporter();
  void customCapturesRenderedInputs_data();
  void customCapturesRenderedInputs();
};

void TestMindMapExport::markdownProjectsWholeForestAndCopiesImage() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const auto bytes = imageBytes(c_localColor);
  QVERIFY(!bytes.isEmpty());
  const auto path = temporary.filePath(QStringLiteral("source/forest.unusual"));
  const auto json = forest();
  QVERIFY(writeBytes(path, json.toUtf8()));
  QVERIFY(writeBytes(temporary.filePath(QStringLiteral("source/image.png")), bytes));
  ServiceLocator services;
  Exporter exporter(services, nativeRegistry(services), nullptr);
  const auto output = exportSingle(
      exporter, options(temporary.filePath(QStringLiteral("out")), ExportFormat::Markdown),
      source(path, QStringLiteral("nAtIvEaLiAs")), json);
  QVERIFY(!output.isEmpty());
  QCOMPARE(QFileInfo(output).fileName(), QStringLiteral("forest.md"));
  const auto markdown = QString::fromUtf8(readBytes(output));
  // Check semantic text, escaped topics and destinations, not Qt's different Markdown dialect.
  QVERIFY2(containsForest(markdown, true), qPrintable(markdown));
  QVERIFY(QRegularExpression(QStringLiteral("(?m)^#{5} .*Fifth level")).match(markdown).hasMatch());
  QVERIFY(markdown.contains(QStringLiteral("id=\"m3-node-6669667468\"")));
  QVERIFY(markdown.contains(QStringLiteral("](#m3-node-6f727068616e2d6368696c64 ")));
  QVERIFY(!markdown.contains(QStringLiteral("](#m3-node-6669667468")));
  const auto image =
      QRegularExpression(QStringLiteral("!\\[[^\\]]*\\]\\(<([^>]+)>\\)")).match(markdown);
  QVERIFY(image.hasMatch());
  const auto relative = QUrl(image.captured(1));
  QVERIFY(relative.isRelative());
  const auto copiedPath = QFileInfo(output).dir().filePath(relative.path(QUrl::FullyDecoded));
  QVERIFY(QFileInfo(copiedPath).absoluteFilePath() !=
          temporary.filePath(QStringLiteral("source/image.png")));
  QCOMPARE(readBytes(copiedPath), bytes);
  QVERIFY(!QImage(copiedPath).isNull());
  QCOMPARE(readBytes(path), json.toUtf8());
}

void TestMindMapExport::markdownRendersDocumentHierarchy() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  QJsonArray nodes;
  for (int level = 1; level <= 8; ++level) {
    QJsonObject node{{QStringLiteral("id"), QString::number(level)},
                     {QStringLiteral("topic"), QStringLiteral("Topic %1").arg(level)}};
    if (level < 8) {
      node.insert(QStringLiteral("children"), QJsonArray{QString::number(level + 1)});
    }
    if (level == 1) {
      node.insert(QStringLiteral("icons"), QJsonArray{QStringLiteral("star"), QString()});
      node.insert(QStringLiteral("tags"),
                  QJsonArray{QStringLiteral("release"), QString(), QStringLiteral(" leading *tag*")});
      node.insert(QStringLiteral("image"), imageObject(QStringLiteral("image.png")));
    }
    if (level == 7) {
      node.insert(QStringLiteral("note"),
                  QStringLiteral("First line\r\nsecond line\r\n\r\n# Literal **text**\n"
                                 "\n    Indented text"));
    }
    if (level == 8) {
      node.insert(QStringLiteral("note"), QStringLiteral("Deep paragraph\n\nStill deep"));
    }
    nodes.append(node);
  }
  nodes.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("orphan")},
                           {QStringLiteral("topic"), QStringLiteral("Separate tree")}});
  const QJsonObject link{{QStringLiteral("id"), QStringLiteral("private-link-id")},
                         {QStringLiteral("source"), QStringLiteral("1")},
                         {QStringLiteral("target"), QStringLiteral("8")},
                         {QStringLiteral("directed"), true},
                         {QStringLiteral("topic"), QStringLiteral("Review dependency")}};
  const auto json = QString::fromUtf8(
      QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"), 1},
                                {QStringLiteral("rootId"), QStringLiteral("1")},
                                {QStringLiteral("nodes"), nodes},
                                {QStringLiteral("orphanIds"), QJsonArray{QStringLiteral("orphan")}},
                                {QStringLiteral("crossLinks"), QJsonArray{link}}})
          .toJson());
  const auto path = temporary.filePath(QStringLiteral("source/document.emind"));
  QVERIFY(writeBytes(path, json.toUtf8()));
  QVERIFY(writeBytes(temporary.filePath(QStringLiteral("source/image.png")), imageBytes(c_localColor)));
  ServiceLocator services;
  Exporter exporter(services, nativeRegistry(services), nullptr);
  const auto output = exportSingle(
      exporter, options(temporary.filePath(QStringLiteral("out")), ExportFormat::Markdown),
      source(path), json);
  QVERIFY(!output.isEmpty());
  QTextDocument document;
  document.setMarkdown(QString::fromUtf8(readBytes(output)));
  QVector<int> levels;
  const QRegularExpression topic(QStringLiteral("^Topic [1-8]$"));
  QMap<QString, QTextBlock> blocks;
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    blocks.insert(block.text(), block);
    if (topic.match(block.text()).hasMatch()) {
      levels.append(block.blockFormat().headingLevel());
    }
  }
  QCOMPARE(levels, (QVector<int>{1, 2, 3, 4, 5, 6, 0, 0}));
  QVERIFY(!blocks[QStringLiteral("Topic 6")].textList());
  const auto *seventh = blocks[QStringLiteral("Topic 7")].textList();
  const auto *eighth = blocks[QStringLiteral("Topic 8")].textList();
  QVERIFY(seventh);
  QVERIFY(eighth);
  QVERIFY(eighth->format().indent() > seventh->format().indent());
  QCOMPARE(blocks[QStringLiteral("Separate tree")].blockFormat().headingLevel(), 1);
  QVERIFY(!blocks[QStringLiteral("Separate tree")].textList());
  // Qt can import hard breaks as separate blocks; assert content and nesting, not that dialect.
  for (const auto &line : {QStringLiteral("First line"), QStringLiteral("second line"),
                           QStringLiteral("Indented text")}) {
    const auto found = document.find(line);
    QVERIFY(!found.isNull());
    QCOMPARE(found.blockFormat().indent(), seventh->format().indent());
  }
  QVERIFY(blocks.contains(QStringLiteral("# Literal **text**")));
  QCOMPARE(blocks[QStringLiteral("# Literal **text**")].blockFormat().indent(),
           seventh->format().indent());
  QCOMPARE(blocks[QStringLiteral("Deep paragraph")].blockFormat().indent(), eighth->format().indent());
  QCOMPARE(blocks[QStringLiteral("Still deep")].blockFormat().indent(), eighth->format().indent());
  const auto text = document.toPlainText();
  QVERIFY(text.contains(QStringLiteral("star")));
  QVERIFY(text.contains(QStringLiteral("#release")));
  QVERIFY(text.contains(QStringLiteral("# leading *tag*")));
  const auto tags = document.find(QStringLiteral("# leading *tag*"));
  QCOMPARE(tags.blockFormat().headingLevel(), 0);
  QVERIFY(!text.contains(QStringLiteral("Review dependency")));
  for (const auto &metadata : {QStringLiteral("private-link-id"), QStringLiteral("Image size:"),
                               QStringLiteral("Image URL:"), QStringLiteral("(empty)"),
                               QStringLiteral("Note:"), QStringLiteral("Icon:"),
                               QStringLiteral("Tag:")}) {
    QVERIFY2(!text.contains(metadata), qPrintable(text));
  }
}

void TestMindMapExport::markdownLinksMatchHtmlDirection_data() {
  QTest::addColumn<bool>("directed");
  QTest::addColumn<QJsonValue>("arrow");
  QTest::addColumn<bool>("self");
  QTest::addColumn<bool>("parallel");
  QTest::addColumn<int>("rootLinks");
  QTest::addColumn<int>("deepLinks");
  QTest::newRow("forward") << true << QJsonValue() << false << false << 1 << 0;
  QTest::newRow("backward") << true << QJsonValue("backward") << false << false << 0 << 1;
  QTest::newRow("both") << true << QJsonValue("both") << false << false << 1 << 1;
  QTest::newRow("undirected") << false << QJsonValue() << false << false << 1 << 1;
  QTest::newRow("undirected-backward") << false << QJsonValue("backward") << false << false << 1 << 1;
  QTest::newRow("opaque-style") << true << QJsonValue(42) << false << false << 1 << 0;
  QTest::newRow("self-backward") << true << QJsonValue("backward") << true << false << 1 << 0;
  QTest::newRow("self-both") << true << QJsonValue("both") << true << false << 1 << 0;
  QTest::newRow("parallel-self") << false << QJsonValue() << true << true << 2 << 0;
}

void TestMindMapExport::markdownLinksMatchHtmlDirection() {
  QFETCH(bool, directed);
  QFETCH(QJsonValue, arrow);
  QFETCH(bool, self);
  QFETCH(bool, parallel);
  QFETCH(int, rootLinks);
  QFETCH(int, deepLinks);
  const auto target = QStringLiteral("target\"<&");
  const auto label = QStringLiteral("Relation \"quoted\" \\ &copy; <tag>\n\nnext");
  const auto secondLabel = QStringLiteral("Another relationship");
  const auto duplicate = QStringLiteral("Same [topic]");
  QJsonArray nodes;
  for (int level = 1; level <= 8; ++level) {
    const auto id = level == 8 ? target : QString::number(level);
    const auto topic = level == 1 || level == 8 ? duplicate : QStringLiteral("Depth %1").arg(level);
    QJsonObject node{{QStringLiteral("id"), id}, {QStringLiteral("topic"), topic}};
    if (level < 8) {
      node.insert(QStringLiteral("children"),
                  QJsonArray{level == 7 ? target : QString::number(level + 1)});
    }
    nodes.append(node);
  }
  QJsonObject link{{QStringLiteral("id"), QStringLiteral("link-a")},
                   {QStringLiteral("source"), QStringLiteral("1")},
                   {QStringLiteral("target"), self ? QStringLiteral("1") : target},
                   {QStringLiteral("directed"), directed},
                   {QStringLiteral("topic"), label},
                   {QStringLiteral("style"), QJsonObject{{QStringLiteral("arrowDirection"), arrow}}}};
  QJsonArray links{link};
  if (parallel) {
    link.insert(QStringLiteral("id"), QStringLiteral("link-b"));
    link.insert(QStringLiteral("topic"), secondLabel);
    links.append(link);
  }
  ExportInput input;
  input.file.fileName = QStringLiteral("relations.emind");
  input.content = QString::fromUtf8(QJsonDocument(QJsonObject{
      {QStringLiteral("schemaVersion"), 1}, {QStringLiteral("rootId"), QStringLiteral("1")},
      {QStringLiteral("nodes"), nodes}, {QStringLiteral("crossLinks"), links}}).toJson());
  ServiceLocator services;
  MindMapExporter renderer(services, nullptr, nullptr);
  ExportMarkdown markdown;
  QVERIFY(renderer.renderMarkdown(input, markdown));
  // Render through VNote's native producer, then inspect actual browser link semantics.
  const auto native = MarkdownHtmlRenderer::render(markdown.content.toUtf8(), {});
  QVERIFY2(native.error.isEmpty(), qPrintable(native.error));
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const auto pagePath = temporary.filePath(QStringLiteral("document.html"));
  QVERIFY(writeBytes(pagePath, QByteArrayLiteral("<!doctype html><meta charset=\"utf-8\"><body>") +
                                   native.html.toUtf8() + QByteArrayLiteral("</body>")));
  HtmlPage page;
  QVERIFY(page.load(pagePath));
  const auto rendered = page.evaluate(QStringLiteral(R"JS((() => {
    const anchors = Array.from(document.querySelectorAll('a[id]'));
    return Array.from(document.querySelectorAll('a[href]'))
      .filter(link => !link.classList.contains('vx-header-anchor') &&
                      link.getAttribute('href').startsWith('#')).map(link => {
      const owner = anchors.filter(anchor => anchor.compareDocumentPosition(link) &
                                              Node.DOCUMENT_POSITION_FOLLOWING).pop();
      const href = link.getAttribute('href');
      const target = document.getElementById(href.slice(1));
      link.click();
      return {owner: owner.id, href, title: link.title, text: link.textContent,
              inOwner: owner.closest('li') === link.closest('li'),
              targetExists: !!target, navigated: location.hash === href};
    });
  })())JS"))
                            .toList();
  QVector<QStringList> actual;
  for (const auto &value : rendered) {
    const auto reference = value.toMap();
    QVERIFY(reference.value(QStringLiteral("inOwner")).toBool());
    QVERIFY(reference.value(QStringLiteral("targetExists")).toBool());
    QVERIFY(reference.value(QStringLiteral("navigated")).toBool());
    QVERIFY(reference.value(QStringLiteral("text")).toString().contains(duplicate));
    actual.append({reference.value(QStringLiteral("owner")).toString(),
                   reference.value(QStringLiteral("href")).toString(),
                   reference.value(QStringLiteral("title")).toString()});
  }
  const auto targetAnchor = QStringLiteral("#m3-node-") + QString::fromLatin1(target.toUtf8().toHex());
  QVector<QStringList> expected;
  for (int i = 0; i < rootLinks; ++i) {
    expected.append({QStringLiteral("m3-node-31"), self ? QStringLiteral("#m3-node-31") : targetAnchor,
                     i == 0 ? label : secondLabel});
  }
  if (deepLinks) {
    expected.append({targetAnchor.mid(1), QStringLiteral("#m3-node-31"), label});
  }
  QCOMPARE(actual, expected);
  const auto text = page.evaluate(QStringLiteral("document.body.textContent")).toString();
  QVERIFY(!text.contains(QStringLiteral("Relation \"quoted\"")));
  QVERIFY(!text.contains(QStringLiteral("Cross-links")));
}

void TestMindMapExport::markdownDoesNotCreateWidgets() {
  ServiceLocator services;
  MindMapExporter renderer(services, nullptr, nullptr);
  const auto widgets = QApplication::allWidgets();
  ExportInput input;
  input.file.fileName = QStringLiteral("headless.emind");
  input.content = forest(QStringLiteral("not-required-for-projection.png"));
  ExportMarkdown markdown;
  QVERIFY(renderer.renderMarkdown(input, markdown));
  QCOMPARE(QApplication::allWidgets(), widgets);
  QVERIFY(containsForest(markdown.content, true));
  QVERIFY(markdown.content.contains(QStringLiteral("not-required-for-projection.png")));
  input.content = QStringLiteral(" \t\r\n");
  QVERIFY(renderer.renderMarkdown(input, markdown));
  QCOMPARE(QApplication::allWidgets(), widgets);
  QVERIFY(markdown.content.contains(QStringLiteral("Central topic")));
}

void TestMindMapExport::unknownAndEmptyTypesUseRegisteredFallback() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  ServiceLocator services;
  ExportRendererFactory registry(services);
  registry.registerRenderer(QStringLiteral("oThErS"),
                            [](ServiceLocator &p_locator, QWidget *p_host, QObject *p_parent) {
                              return new MindMapExporter(p_locator, p_host, p_parent);
                            });
  Exporter exporter(services, registry, nullptr);
  const auto option = options(temporary.filePath(QStringLiteral("out")), ExportFormat::Markdown);
  for (const auto &type : {QString(), QStringLiteral("UnregisteredSource")}) {
    const auto output = exportSingle(
        exporter, option, source(temporary.filePath(QStringLiteral("fallback.not-native")), type),
        simpleMap(QStringLiteral("Actual fallback projection")));
    QVERIFY(!output.isEmpty());
    QCOMPARE(QFileInfo(output).fileName(), QStringLiteral("fallback.md"));
    QVERIFY(readBytes(output).contains("Actual fallback projection"));
    QVERIFY(!readBytes(output).contains("schemaVersion"));
  }
}

void TestMindMapExport::snapshotsAreAuthoritative_data() {
  QTest::addColumn<int>("format");
  QTest::newRow("markdown") << int(ExportFormat::Markdown);
  QTest::newRow("html") << int(ExportFormat::HTML);
}

void TestMindMapExport::snapshotsAreAuthoritative() {
  QFETCH(int, format);
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const auto path = temporary.filePath(QStringLiteral("source/snapshot.emind"));
  const auto disk = simpleMap(QStringLiteral("DISK snapshot only"));
  const auto unsaved = simpleMap(QStringLiteral("UNSAVED live snapshot"));
  QVERIFY(writeBytes(path, disk.toUtf8()));
  ServiceLocator services;
  Exporter exporter(services, nativeRegistry(services), nullptr);
  const auto option = options(temporary.filePath(QStringLiteral("out")), ExportFormat(format));
  const auto live = exportSingle(exporter, option, source(path), unsaved);
  QVERIFY(!live.isEmpty());
  QVERIFY(readBytes(live).contains("UNSAVED live snapshot"));
  QVERIFY(!readBytes(live).contains("DISK snapshot only"));
  const auto empty = exportSingle(exporter, option, source(path), QString());
  QVERIFY(!empty.isEmpty());
  QVERIFY(readBytes(empty).contains("Central topic"));
  QVERIFY(!readBytes(empty).contains("DISK snapshot only"));
  QVERIFY(!readBytes(empty).contains("UNSAVED live snapshot"));
  const auto batch = exporter.doExportBatch(option, {source(path)}, QStringLiteral("disk batch"));
  QCOMPARE(batch.size(), 1);
  QVERIFY(readBytes(batch.first()).contains("DISK snapshot only"));
  QVERIFY(!readBytes(batch.first()).contains("UNSAVED live snapshot"));
  QCOMPARE(readBytes(path), disk.toUtf8());
}

void TestMindMapExport::standaloneContainsBothImagesAndCompleteArticles() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const auto path = temporary.filePath(QStringLiteral("source/forest.emind"));
  const auto embedded = QStringLiteral("data:image/png;base64,") +
                        QString::fromLatin1(imageBytes(c_embeddedColor).toBase64());
  const auto json = forest(QStringLiteral("image.png"), embedded);
  QVERIFY(writeBytes(path, json.toUtf8()));
  QVERIFY(
      writeBytes(temporary.filePath(QStringLiteral("source/image.png")), imageBytes(c_localColor)));
  ServiceLocator services;
  Exporter exporter(services, nativeRegistry(services), nullptr);
  const auto output =
      exportSingle(exporter, options(temporary.filePath(QStringLiteral("out")), ExportFormat::HTML),
                   source(path), json);
  QVERIFY(!output.isEmpty());
  const auto html = QString::fromUtf8(readBytes(output));
  const auto images = embeddedNodeImages(html);
  QCOMPARE(images.size(), 2);
  QCOMPARE(embeddedNodeImages(html, QStringLiteral("m3-article-image")), images);
  for (const auto &image : images) {
    QCOMPARE(image.size(), QSize(48, 32));
  }
  QVERIFY(containsImageColor(html, c_localColor));
  QVERIFY(containsImageColor(html, c_embeddedColor));
  QVERIFY(containsForest(htmlText(html)));
  // Prove the standalone artifact needs neither the source JSON nor local image on reload.
  QVERIFY(QDir(temporary.filePath(QStringLiteral("source"))).removeRecursively());
  HtmlPage page;
  QVERIFY(page.load(output));
  auto state = page.presentation();
  QCOMPARE(state.value(QStringLiteral("articleTopics")).toStringList(), forestTopics());
  QCOMPARE(state.value(QStringLiteral("treeEdges")).toInt(), 5);
  QCOMPARE(state.value(QStringLiteral("crossLinks")).toInt(), 1);
  QVERIFY(state.value(QStringLiteral("map")).toBool());
  QVERIFY(state.value(QStringLiteral("image")).toBool());
  QVERIFY(state.value(QStringLiteral("articles")).toBool());
  QVERIFY(state.value(QStringLiteral("articleImages")).toBool());
  QVERIFY(containsForest(state.value(QStringLiteral("text")).toString()));
}

void TestMindMapExport::sectionNumberingReflectsSettingsOnReuse_data() {
  QTest::addColumn<bool>("initiallyEnabled");
  QTest::newRow("initially-enabled") << true;
  QTest::newRow("initially-disabled") << false;
}

void TestMindMapExport::sectionNumberingReflectsSettingsOnReuse() {
  QFETCH(bool, initiallyEnabled);
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  VxCoreContextHandle context = nullptr;
  QCOMPARE(vxcore_context_create("{}", &context), VXCORE_OK);
  const auto destroyContext = qScopeGuard([&] { vxcore_context_destroy(context); });
  ConfigCoreService configCore(context);
  ConfigMgr2 config(&configCore);
  config.init();
  ServiceLocator services;
  services.registerService(&config);
  MindMapExporter renderer(services, nullptr, nullptr);
  auto &editorSettings = config.getEditorConfig();
  auto &mindMapSettings = editorSettings.getMindMapEditorConfig();
  const auto originalPattern = editorSettings.getSectionNumberPattern();
  const auto originalEnabled = mindMapSettings.getAutoSectionNumbering();
  const auto restoreSettings = qScopeGuard([&] {
    editorSettings.setSectionNumberPattern(originalPattern);
    mindMapSettings.setAutoSectionNumbering(originalEnabled);
  });

  auto document = QJsonDocument::fromJson(forest(QString()).toUtf8()).object();
  auto nodes = document.value(QStringLiteral("nodes")).toArray();
  nodes.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("another-orphan")},
                           {QStringLiteral("topic"), QStringLiteral("Another orphan")},
                           {QStringLiteral("children"), QJsonArray{QStringLiteral("authored")}}});
  nodes.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("authored")},
                           {QStringLiteral("topic"), QStringLiteral("1. Authored child")}});
  document.insert(QStringLiteral("nodes"), nodes);
  document.insert(QStringLiteral("orphanIds"),
                  QJsonArray{QStringLiteral("orphan"), QStringLiteral("another-orphan")});
  ExportInput input;
  input.file = source(temporary.filePath(QStringLiteral("numbered.emind")));
  input.content = QString::fromUtf8(QJsonDocument(document).toJson(QJsonDocument::Compact));
  auto authoredTopics = forestTopics();
  authoredTopics.append(QStringLiteral("Another orphan"));
  authoredTopics.append(QStringLiteral("1. Authored child"));
  const QStringList numbers{QString(), QStringLiteral("1"), QStringLiteral("1.1"),
                            QStringLiteral("1.1.1"), QStringLiteral("1.1.1.1"), QString(),
                            QStringLiteral("1"), QString(), QStringLiteral("1")};
  ExportHtmlRequest request;
  request.purpose = ExportHtmlPurpose::Standalone;
  HtmlPage page;
  struct NumberingSettings {
    bool enabled;
    QString pattern;
    QString suffix;
  };
  const NumberingSettings settings[] = {
      {initiallyEnabled, QStringLiteral("1.1)"), QStringLiteral(")")},
      {true, QStringLiteral("1.1"), QString()},
      {false, QStringLiteral("1.1"), QString()},
      {true, QStringLiteral("1.1."), QStringLiteral(".")}};
  for (int index = 0; index < 4; ++index) {
    const auto &setting = settings[index];
    mindMapSettings.setAutoSectionNumbering(setting.enabled);
    editorSettings.setSectionNumberPattern(setting.pattern);
    const auto output = temporary.filePath(QStringLiteral("numbered-%1.html").arg(index));
    QVERIFY(renderer.renderHtml(input, request, output));
    QVERIFY(page.load(output));
    const auto state = page.presentation();
    auto visualTopics = authoredTopics;
    if (setting.enabled) {
      for (int node = 0; node < visualTopics.size(); ++node) {
        if (!numbers[node].isEmpty()) {
          visualTopics[node].prepend(numbers[node] + setting.suffix + QLatin1Char(' '));
        }
      }
    }
    QCOMPARE(state.value(QStringLiteral("topics")).toStringList(), visualTopics);
    QCOMPARE(state.value(QStringLiteral("articleTopics")).toStringList(), authoredTopics);
  }
}

void TestMindMapExport::batchRebasesSameRelativeImage() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const auto first = temporary.filePath(QStringLiteral("first/map.emind"));
  const auto second = temporary.filePath(QStringLiteral("second/map.emind"));
  QVERIFY(writeBytes(
      first, simpleMap(QStringLiteral("First folder"), QStringLiteral("image.png")).toUtf8()));
  QVERIFY(writeBytes(
      second, simpleMap(QStringLiteral("Second folder"), QStringLiteral("image.png")).toUtf8()));
  QVERIFY(writeBytes(QFileInfo(first).dir().filePath(QStringLiteral("image.png")),
                     imageBytes(c_localColor)));
  QVERIFY(writeBytes(QFileInfo(second).dir().filePath(QStringLiteral("image.png")),
                     imageBytes(c_secondColor)));
  ServiceLocator services;
  Exporter exporter(services, nativeRegistry(services), nullptr);
  const auto outputs =
      exporter.doExportBatch(options(temporary.filePath(QStringLiteral("out")), ExportFormat::HTML),
                             {source(first), source(second)}, QStringLiteral("same names"));
  QCOMPARE(outputs.size(), 2);
  QVERIFY(outputs[0] != outputs[1]);
  const auto firstHtml = QString::fromUtf8(readBytes(outputs[0]));
  const auto secondHtml = QString::fromUtf8(readBytes(outputs[1]));
  QVERIFY(firstHtml.contains(QStringLiteral("First folder")));
  QVERIFY(secondHtml.contains(QStringLiteral("Second folder")));
  QVERIFY(containsImageColor(firstHtml, c_localColor));
  QVERIFY(!containsImageColor(firstHtml, c_secondColor));
  QVERIFY(containsImageColor(secondHtml, c_secondColor));
  QVERIFY(!containsImageColor(secondHtml, c_localColor));
}

void TestMindMapExport::malformedAndCollidingInputs_data() { snapshotsAreAuthoritative_data(); }

void TestMindMapExport::malformedAndCollidingInputs() {
  QFETCH(int, format);
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const auto good = temporary.filePath(QStringLiteral("first/same.emind"));
  const auto other = temporary.filePath(QStringLiteral("second/same.emind"));
  const auto invalid = temporary.filePath(QStringLiteral("third/broken.emind"));
  QVERIFY(writeBytes(good, simpleMap(QStringLiteral("FIRST preserved")).toUtf8()));
  QVERIFY(writeBytes(other, simpleMap(QStringLiteral("SECOND preserved")).toUtf8()));
  QVERIFY(writeBytes(invalid, QByteArrayLiteral("{malformed native map")));
  ServiceLocator services;
  Exporter exporter(services, nativeRegistry(services), nullptr);
  QSignalSpy logs(&exporter, &Exporter::logRequested);
  const auto option = options(temporary.filePath(QStringLiteral("out")), ExportFormat(format));
  QVERIFY(exportSingle(exporter, option, source(invalid), QString::fromUtf8(readBytes(invalid)))
              .isEmpty());
  QVERIFY(outputFiles(option.m_outputDir).isEmpty());
  QVERIFY(!logs.isEmpty());
  const auto first =
      exportSingle(exporter, option, source(good), QString::fromUtf8(readBytes(good)));
  QVERIFY(!first.isEmpty());
  const auto preserved = readBytes(first);
  const auto second =
      exportSingle(exporter, option, source(other), QString::fromUtf8(readBytes(other)));
  QVERIFY(!second.isEmpty());
  QVERIFY(first != second);
  QCOMPARE(readBytes(first), preserved);
  QVERIFY(readBytes(first).contains("FIRST preserved"));
  QVERIFY(readBytes(second).contains("SECOND preserved"));
  const auto batch = exporter.doExportBatch(option, {source(good), source(invalid), source(other)},
                                            QStringLiteral("mixed batch"));
  QCOMPARE(batch.size(), 2);
  QVERIFY(batch[0] != batch[1]);
  QVERIFY(readBytes(batch[0]).contains("FIRST preserved"));
  QVERIFY(readBytes(batch[1]).contains("SECOND preserved"));
  QCOMPARE(outputFiles(QFileInfo(batch.first()).dir().absolutePath()).size(),
           format == int(ExportFormat::HTML) ? 2 : 1);
  for (const auto &file : outputFiles(option.m_outputDir)) {
    QVERIFY(!readBytes(file).contains("{malformed native map"));
  }
}

void TestMindMapExport::delayedImageCompletesBeforeSnapshot() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  HeldImageServer server;
  QVERIFY(server.listen());
  ServiceLocator services;
  Exporter exporter(services, nativeRegistry(services), nullptr);
  const auto option = options(temporary.filePath(QStringLiteral("out")), ExportFormat::HTML);
  const auto heldWithoutOutput = std::make_shared<bool>(false);
  connect(&server, &HeldImageServer::requestReceived, &server,
          [&server, heldWithoutOutput, option] {
            *heldWithoutOutput = !server.released() && outputFiles(option.m_outputDir).isEmpty();
            QTimer::singleShot(0, &server, [&server] { server.release(imageBytes(c_localColor)); });
          });
  const auto output =
      exportSingle(exporter, option, source(temporary.filePath(QStringLiteral("delay.emind"))),
                   simpleMap(QStringLiteral("Delayed image"), server.url()));
  QVERIFY(server.received());
  QVERIFY(*heldWithoutOutput);
  QVERIFY(server.released());
  QVERIFY(!output.isEmpty());
  QVERIFY(containsImageColor(QString::fromUtf8(readBytes(output)), c_localColor));
}

void TestMindMapExport::failedImagesSettleWithPlaceholderAndWarning() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  QVERIFY(writeBytes(temporary.filePath(QStringLiteral("invalid.png")),
                     QByteArrayLiteral("not an image")));
  ServiceLocator services;
  Exporter exporter(services, nativeRegistry(services), nullptr);
  QSignalSpy logs(&exporter, &Exporter::logRequested);
  const auto option = options(temporary.filePath(QStringLiteral("out")), ExportFormat::HTML);
  const auto info = source(temporary.filePath(QStringLiteral("failure.emind")));
  const auto missing =
      exportSingle(exporter, option, info,
                   simpleMap(QStringLiteral("Settled image"), QStringLiteral("missing.png")));
  QVERIFY(!missing.isEmpty());
  QVERIFY(loggedImageUrl(logs, QStringLiteral("missing.png")));
  const auto missingHtml = QString::fromUtf8(readBytes(missing));
  QCOMPARE(imageStateCount(missingHtml, QStringLiteral("unavailable")), 2);
  QCOMPARE(imageStateCount(missingHtml, QStringLiteral("loading")), 0);
  QVERIFY(embeddedNodeImages(missingHtml).isEmpty());
  logs.clear();
  const auto invalid =
      exportSingle(exporter, option, info,
                   simpleMap(QStringLiteral("Settled image"), QStringLiteral("invalid.png")));
  QVERIFY(!invalid.isEmpty());
  QVERIFY(loggedImageUrl(logs, QStringLiteral("invalid.png")));
  const auto invalidHtml = QString::fromUtf8(readBytes(invalid));
  QCOMPARE(imageStateCount(invalidHtml, QStringLiteral("unavailable")), 2);
  QCOMPARE(imageStateCount(invalidHtml, QStringLiteral("loading")), 0);

  // An asynchronous decode failure must also settle, rather than exporting a loading state.
  // A map without an image has no placeholder.
  HeldImageServer server;
  QVERIFY(server.listen());
  connect(&server, &HeldImageServer::requestReceived, &server, [&server] {
    QTimer::singleShot(0, &server, [&server] { server.release(QByteArrayLiteral("invalid PNG")); });
  });
  logs.clear();
  const auto remote = exportSingle(exporter, option, info,
                                   simpleMap(QStringLiteral("Settled image"), server.url()));
  QVERIFY(!remote.isEmpty());
  QVERIFY(server.received() && server.released());
  QVERIFY(loggedImageUrl(logs, server.url()));
  const auto remoteHtml = QString::fromUtf8(readBytes(remote));
  QCOMPARE(imageStateCount(remoteHtml, QStringLiteral("unavailable")), 2);
  QCOMPARE(imageStateCount(remoteHtml, QStringLiteral("loading")), 0);
  const auto absent =
      exportSingle(exporter, option, info, simpleMap(QStringLiteral("Settled image")));
  QVERIFY(!absent.isEmpty());
  const auto absentHtml = QString::fromUtf8(readBytes(absent));
  QCOMPARE(imageStateCount(absentHtml, QStringLiteral("unavailable")), 0);
  QCOMPARE(imageStateCount(absentHtml, QStringLiteral("loading")), 0);
  QVERIFY(embeddedNodeImages(absentHtml).isEmpty());
}

void TestMindMapExport::cancellationDoesNotPublishLateOutput() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  HeldImageServer server;
  QVERIFY(server.listen());
  ServiceLocator services;
  Exporter exporter(services, nativeRegistry(services), nullptr);
  const auto option = options(temporary.filePath(QStringLiteral("out")), ExportFormat::HTML);
  const auto info = source(temporary.filePath(QStringLiteral("cancel.emind")));
  const QPointer<Exporter> guarded(&exporter);
  connect(&server, &HeldImageServer::requestReceived, &server, [guarded] {
    if (guarded) {
      guarded->stop();
    }
  });
  const auto result = exportSingle(exporter, option, info,
                                   simpleMap(QStringLiteral("Cancelled image"), server.url()));
  QVERIFY(server.received());
  QVERIFY(!server.released());
  QVERIFY(result.isEmpty());
  QVERIFY(outputFiles(option.m_outputDir).isEmpty());
  server.release(imageBytes(c_localColor));
  QVERIFY(QTest::qWaitFor([&server] { return server.disconnected(); }, 10000));
  const auto drained = std::make_shared<bool>(false);
  QTimer::singleShot(0, &server, [drained] { *drained = true; });
  QVERIFY(QTest::qWaitFor([drained] { return *drained; }, 10000));
  QVERIFY(outputFiles(option.m_outputDir).isEmpty());
  const auto recovered =
      exportSingle(exporter, option, info, simpleMap(QStringLiteral("New operation after cancel")));
  QVERIFY(!recovered.isEmpty());
  QVERIFY(readBytes(recovered).contains("New operation after cancel"));
  QVERIFY(!readBytes(recovered).contains("Cancelled image"));
  QCOMPARE(outputFiles(option.m_outputDir).size(), 1);
}

void TestMindMapExport::directConsumerRequests_data() {
  QTest::addColumn<int>("purpose");
  QTest::newRow("pdf") << int(ExportHtmlPurpose::Pdf);
  QTest::newRow("custom") << int(ExportHtmlPurpose::Custom);
}

void TestMindMapExport::directConsumerRequests() {
  QFETCH(int, purpose);
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  QVERIFY(writeBytes(temporary.filePath(QStringLiteral("image.png")), imageBytes(c_localColor)));
  ServiceLocator services;
  MindMapExporter renderer(services, nullptr, nullptr);
  ExportInput input;
  input.file = source(temporary.filePath(QStringLiteral("full.emind")));
  input.content = forest();
  ExportHtmlRequest request;
  request.purpose = ExportHtmlPurpose(purpose);
  request.options.m_pdfOption.m_layout.reset(
      new QPageLayout(QPageSize(QPageSize::A4), QPageLayout::Portrait, QMarginsF(12, 15, 12, 15),
                      QPageLayout::Millimeter));
  const auto output = temporary.filePath(QStringLiteral("consumer.html"));
  QVERIFY(renderer.renderHtml(input, request, output));
  QVERIFY(containsImageColor(QString::fromUtf8(readBytes(output)), c_localColor));
  HtmlPage page;
  QVERIFY(page.load(output));
  const auto state = page.presentation();
  QVERIFY(state.value(QStringLiteral("map")).toBool());
  QVERIFY(state.value(QStringLiteral("articles")).toBool());
  QVERIFY(state.value(QStringLiteral("image")).toBool());
  QVERIFY(state.value(QStringLiteral("articleImages")).toBool());
  QVERIFY(containsForest(state.value(QStringLiteral("text")).toString()));
  const auto links = state.value(QStringLiteral("links")).toStringList();
  QVERIFY(links.contains(QStringLiteral("https://example.test/deep")));
  QVERIFY(links.contains(QStringLiteral("#m3-node-6f727068616e2d6368696c64")));
}

void TestMindMapExport::qtPdfRunsThroughExporter() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const auto path = temporary.filePath(QStringLiteral("source/map.emind"));
  const auto json = forest();
  QVERIFY(writeBytes(path, json.toUtf8()));
  QVERIFY(
      writeBytes(temporary.filePath(QStringLiteral("source/image.png")), imageBytes(c_localColor)));
  ServiceLocator services;
  Exporter exporter(services, nativeRegistry(services), nullptr);
  auto option = options(temporary.filePath(QStringLiteral("out")), ExportFormat::PDF);
  option.m_pdfOption.m_layout.reset(new QPageLayout(QPageSize(QPageSize::A4), QPageLayout::Portrait,
                                                    QMarginsF(12, 15, 12, 15),
                                                    QPageLayout::Millimeter));
  option.m_pdfOption.m_useWkhtmltopdf = false;
  const auto output = exportSingle(exporter, option, source(path), json);
  QVERIFY(!output.isEmpty());
  QCOMPARE(QFileInfo(output).suffix(), QStringLiteral("pdf"));
  const auto pdf = readBytes(output);
  QVERIFY(pdf.startsWith("%PDF-"));
  QVERIFY(pdf.trimmed().endsWith("%%EOF"));
  QCOMPARE(readBytes(path), json.toUtf8());
}

void TestMindMapExport::customCapturesRenderedInputs_data() {
  QTest::addColumn<bool>("allInOne");
  QTest::newRow("single") << false;
  QTest::newRow("all-in-one") << true;
}

void TestMindMapExport::customCapturesRenderedInputs() {
  QFETCH(bool, allInOne);
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const auto first = temporary.filePath(QStringLiteral("first source/map odd.emind"));
  const auto second = temporary.filePath(QStringLiteral("second source/map odd.emind"));
  const auto json = forest();
  const auto secondJson =
      simpleMap(QStringLiteral("Second custom disk snapshot"), QStringLiteral("image.png"));
  QVERIFY(writeBytes(first, json.toUtf8()));
  QVERIFY(writeBytes(second, secondJson.toUtf8()));
  QVERIFY(writeBytes(QFileInfo(first).dir().filePath(QStringLiteral("image.png")),
                     imageBytes(c_localColor)));
  QVERIFY(writeBytes(QFileInfo(second).dir().filePath(QStringLiteral("image.png")),
                     imageBytes(c_secondColor)));
  ServiceLocator services;
  Exporter exporter(services, nativeRegistry(services), nullptr);
  ExportCustomOption custom;
  custom.m_targetSuffix = QStringLiteral("capture.json");
  custom.m_useHtmlInput = true;
  custom.m_allInOne = allInOne;
  custom.m_command = QLatin1Char('"') +
                     QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) +
                     QStringLiteral("\" --capture-export-inputs %5 %1");
  auto option = options(temporary.filePath(QStringLiteral("custom output")), ExportFormat::Custom);
  option.m_customOption = &custom;
  QString manifest;
  if (allInOne) {
    const auto outputs =
        exporter.doExportBatch(option, {source(first), source(second)}, QStringLiteral("all maps"));
    QCOMPARE(outputs.size(), 1);
    manifest = outputs.first();
  } else {
    manifest = exportSingle(exporter, option, source(first), json);
  }
  QVERIFY(!manifest.isEmpty());
  QJsonParseError error;
  const auto captured = QJsonDocument::fromJson(readBytes(manifest), &error);
  QCOMPARE(error.error, QJsonParseError::NoError);
  QVERIFY(captured.isArray());
  const auto inputs = captured.array();
  QCOMPARE(inputs.size(), allInOne ? 2 : 1);
  // Source resources are also removed: a copied native document must retain its visual map.
  QVERIFY(QDir(QFileInfo(first).absolutePath()).removeRecursively());
  QVERIFY(QDir(QFileInfo(second).absolutePath()).removeRecursively());
  HtmlPage page;
  for (int i = 0; i < inputs.size(); ++i) {
    const auto entry = inputs[i].toObject();
    const auto original = entry.value(QStringLiteral("source")).toString();
    const auto copy = entry.value(QStringLiteral("copy")).toString();
    QVERIFY(!original.isEmpty());
    QVERIFY(!QFileInfo::exists(original));
    QVERIFY(!QFileInfo::exists(QFileInfo(original).absolutePath()));
    QVERIFY(QFileInfo(copy).isFile());
    QVERIFY(copy != original);
    const auto html = QString::fromUtf8(readBytes(copy));
    QVERIFY(containsImageColor(html, i == 0 ? c_localColor : c_secondColor));
    for (const auto &resource : entry.value(QStringLiteral("resources")).toArray()) {
      const auto paths = resource.toObject();
      QVERIFY(!QFileInfo::exists(paths.value(QStringLiteral("source")).toString()));
      QVERIFY(QDir(paths.value(QStringLiteral("copy")).toString()).exists());
    }
    QVERIFY(page.load(copy));
    const auto state = page.presentation();
    QVERIFY(state.value(QStringLiteral("map")).toBool());
    QVERIFY(state.value(QStringLiteral("image")).toBool());
    QVERIFY(state.value(QStringLiteral("articles")).toBool());
    QVERIFY(state.value(QStringLiteral("articleImages")).toBool());
    if (i == 0) {
      QVERIFY(containsForest(state.value(QStringLiteral("text")).toString()));
    } else {
      QVERIFY(state.value(QStringLiteral("text"))
                  .toString()
                  .contains(QStringLiteral("Second custom disk snapshot")));
    }
  }
}

int main(int argc, char **argv) {
  vxcore_set_test_mode(1);
  if (argc > 1 && QByteArray(argv[1]) == QByteArrayLiteral("--capture-export-inputs")) {
    QCoreApplication application(argc, argv);
    return captureExportInputs(application.arguments());
  }
#ifdef Q_OS_WIN
  if (qgetenv("QT_QPA_PLATFORM") == "offscreen" && qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR")) {
    qputenv("QT_QPA_FONTDIR", qgetenv("WINDIR") + "/Fonts");
  }
#endif
  QApplication application(argc, argv);
  QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::NoProxy));
  TestMindMapExport test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_mindmapexport.moc"
