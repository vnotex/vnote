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

QStringList topics(bool p_tall = false) {
  QStringList labels{QString::fromUtf8("Root 世界 & <map>"),
                     QStringLiteral("Level two"),
                     QString::fromUtf8("Level three\nstyled 世界 <&>"),
                     QStringLiteral("Level four"),
                     QStringLiteral("Level five"),
                     QStringLiteral("Level six"),
                     QStringLiteral("Level seven"),
                     QString::fromUtf8("Deep café eight"),
                     QStringLiteral("Separate orphan tree"),
                     QString::fromUtf8("שלום 42 orphan")};
  if (p_tall) {
    for (int i = 0; i < 48; ++i) {
      labels.append(QStringLiteral("Additional tall-map branch %1").arg(i));
    }
  }
  return labels;
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

ExportInput nativeInput(const QString &p_directory, bool p_tall = false) {
  const auto labels = topics(p_tall);
  QJsonArray nodes;
  for (int i = 0; i < labels.size(); ++i) {
    QJsonObject node{{QStringLiteral("id"), QStringLiteral("n%1").arg(i)},
                     {QStringLiteral("topic"), labels.at(i)}};
    if (i < 7 || i == 8) {
      node.insert(QStringLiteral("children"), QJsonArray{QStringLiteral("n%1").arg(i + 1)});
    }
    if (i == 0 && p_tall) {
      auto children = node.value(QStringLiteral("children")).toArray();
      for (int child = 10; child < labels.size(); ++child) {
        children.append(QStringLiteral("n%1").arg(child));
      }
      node.insert(QStringLiteral("children"), children);
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
      node.insert(QStringLiteral("tags"),
                  QJsonArray{QStringLiteral("Research <&>"), QString(),
                             QStringLiteral("Research <&>")});
      node.insert(QStringLiteral("icons"),
                  QJsonArray{QStringLiteral("star"), QString(), QStringLiteral("bookmark")});
    }
    if (i == 4) {
      node.insert(QStringLiteral("note"),
                  QStringLiteral("First paragraph <b>is plain text</b>\nsecond line\n\n"
                                 "Second paragraph & preserved"));
      node.insert(QStringLiteral("hyperLink"), QStringLiteral("javascript:alert('unsafe')"));
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

bool settleLayout(QWebEnginePage &p_page) {
  if (!evaluateJavaScript(p_page, QStringLiteral(R"JS((() => {
    window.testLayoutSettled = false;
    requestAnimationFrame(() => requestAnimationFrame(() => { window.testLayoutSettled = true; }));
    return true;
  })())JS")).toBool()) {
    return false;
  }
  const QPointer<QWebEnginePage> page(&p_page);
  return QTest::qWaitFor([page] {
    return page && evaluateJavaScript(*page, QStringLiteral("window.testLayoutSettled === true"))
                       .toBool();
  }, 5000);
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
      15000) && settleLayout(p_page);
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

QJsonObject viewerState(QWebEnginePage &p_page) {
  const auto result = evaluateJavaScript(p_page, QStringLiteral(R"JS((() => {
    const viewer = document.getElementById('map-viewer');
    const viewport = document.getElementById('map-viewport');
    const space = document.getElementById('map-space');
    const map = document.getElementById('mindmap');
    const root = document.getElementById(viewer.dataset.mainNode).querySelector('.m3-topic');
    const box = map.getBoundingClientRect(), frame = viewport.getBoundingClientRect();
    const spacer = space.getBoundingClientRect(), main = root.getBoundingClientRect();
    const scale = box.width / Number(map.dataset.width);
    const left = frame.left + viewport.clientLeft, top = frame.top + viewport.clientTop;
    const button = id => document.getElementById(id);
    const controls = button('map-controls');
    const overlay = controls ? controls.getBoundingClientRect() : null;
    let ancestorsUnscaled = true;
    for (let element = map.parentElement; element; element = element.parentElement) {
      const style = getComputedStyle(element);
      ancestorsUnscaled = ancestorsUnscaled && style.transform === 'none' &&
        (style.zoom === 'normal' || Number(style.zoom) === 1);
    }
    return JSON.stringify({
      scale:scale, centerX:(left + viewport.clientWidth / 2 - box.left) / scale,
      centerY:(top + viewport.clientHeight / 2 - box.top) / scale,
      insetX:box.left - spacer.left, insetY:box.top - spacer.top,
      clientWidth:viewport.clientWidth, clientHeight:viewport.clientHeight,
      scrollLeft:viewport.scrollLeft, scrollTop:viewport.scrollTop,
      maxScrollLeft:viewport.scrollWidth - viewport.clientWidth,
      maxScrollTop:viewport.scrollHeight - viewport.clientHeight,
      spaceWidth:spacer.width, spaceHeight:spacer.height,
      mapWidth:box.width, mapHeight:box.height,
      rootX:(main.left + main.width / 2 - box.left) / scale,
      rootY:(main.top + main.height / 2 - box.top) / scale,
      rootVisible:main.left >= left - 1 && main.top >= top - 1 &&
        main.right <= left + viewport.clientWidth + 1 &&
        main.bottom <= top + viewport.clientHeight + 1,
      controlsOverlay:!!overlay && overlay.left >= frame.left && overlay.right <= frame.right &&
        overlay.top >= frame.top && overlay.bottom <= frame.bottom,
      controlTop:overlay ? overlay.top - frame.top : 0,
      controlWidth:overlay ? overlay.width : 0,
      zoomText:button('map-zoom-level') ? button('map-zoom-level').textContent : '',
      inDisabled:button('map-zoom-in') ? button('map-zoom-in').disabled : false,
      outDisabled:button('map-zoom-out') ? button('map-zoom-out').disabled : false,
      transform:getComputedStyle(map).transform, zoom:getComputedStyle(map).zoom,
      ancestorsUnscaled:ancestorsUnscaled,
      overflow:getComputedStyle(viewport).overflow,
      noScroll:[viewer, viewport, space].every(element =>
        element.scrollLeft === 0 && element.scrollTop === 0 &&
        element.scrollWidth <= element.clientWidth + 1 &&
        element.scrollHeight <= element.clientHeight + 1)
    });
  })())JS"));
  return QJsonDocument::fromJson(result.toString().toUtf8()).object();
}

bool activateControl(QWebEnginePage &p_page, const QString &p_id, int p_count = 1) {
  return evaluateJavaScript(p_page, QStringLiteral(R"JS((() => {
    const button = document.getElementById('%1');
    if (!button) return false;
    for (let index = 0; index < %2; ++index) button.click();
    return true;
  })())JS").arg(p_id).arg(p_count)).toBool() && settleLayout(p_page);
}

bool clickElement(BrowserPage &p_browser, const QString &p_selector) {
  const auto selector = QString::fromUtf8(QJsonDocument(QJsonArray{p_selector})
                                            .toJson(QJsonDocument::Compact));
  const auto point = evaluateJavaScript(p_browser.page, QStringLiteral(R"JS((() => {
    const element = document.querySelector(%1[0]);
    if (!element) return null;
    element.scrollIntoView({block:'center', inline:'center'});
    const rect = element.getBoundingClientRect();
    const x = rect.left + rect.width / 2, y = rect.top + rect.height / 2;
    const hit = document.elementFromPoint(x, y);
    return hit && (hit === element || element.contains(hit)) ? [x,y] : null;
  })())JS").arg(selector)).toList();
  if (point.size() != 2 || !p_browser.view.focusProxy()) {
    return false;
  }
  const QPoint position(qRound(point[0].toDouble()), qRound(point[1].toDouble()));
  auto *target = p_browser.view.focusProxy();
  QTest::mouseClick(target, Qt::LeftButton, Qt::NoModifier,
                    target->mapFrom(&p_browser.view, position));
  return settleLayout(p_browser.page);
}

void verifyCenterPreserved(const QJsonObject &p_before, const QJsonObject &p_after) {
  const double scale = p_after.value(QStringLiteral("scale")).toDouble();
  for (const auto &axis : {QStringLiteral("X"), QStringLiteral("Y")}) {
    const bool horizontal = axis == QStringLiteral("X");
    const auto extent = horizontal ? QStringLiteral("clientWidth") : QStringLiteral("clientHeight");
    const auto offset = horizontal ? QStringLiteral("scrollLeft") : QStringLiteral("scrollTop");
    const auto maximum = horizontal ? QStringLiteral("maxScrollLeft") : QStringLiteral("maxScrollTop");
    const double desired = p_before.value(QStringLiteral("center") + axis).toDouble() * scale +
                           p_after.value(QStringLiteral("inset") + axis).toDouble() -
                           p_after.value(extent).toDouble() / 2;
    const double expected = qBound(0.0, desired, p_after.value(maximum).toDouble());
    QVERIFY2(std::abs(p_after.value(offset).toDouble() - expected) < 2,
             qPrintable(QStringLiteral("Zoom/resize moved the map center on axis %1").arg(axis)));
  }
}

void verifyStaticViewer(const QJsonObject &p_state, const QJsonObject &p_viewer) {
  QCOMPARE(p_state.value(QStringLiteral("mode")).toString(), QStringLiteral("static"));
  QVERIFY(!p_state.value(QStringLiteral("controlsExist")).toBool());
  QVERIFY(p_viewer.value(QStringLiteral("noScroll")).toBool());
  QVERIFY(p_viewer.value(QStringLiteral("ancestorsUnscaled")).toBool());
  QCOMPARE(p_viewer.value(QStringLiteral("transform")).toString(), QStringLiteral("none"));
  const double scale = p_viewer.value(QStringLiteral("scale")).toDouble();
  QVERIFY(scale > 0 && scale <= 1.001);
  const auto zoom = p_viewer.value(QStringLiteral("zoom")).toString();
  QVERIFY(std::abs((zoom == QStringLiteral("normal") ? 1 : zoom.toDouble()) - scale) < 0.001);
  QVERIFY(std::abs(p_viewer.value(QStringLiteral("mapWidth")).toDouble() -
                   p_viewer.value(QStringLiteral("spaceWidth")).toDouble()) < 1);
  QVERIFY(std::abs(p_viewer.value(QStringLiteral("mapHeight")).toDouble() -
                   p_viewer.value(QStringLiteral("spaceHeight")).toDouble()) < 1);
}

QJsonObject documentState(QWebEnginePage &p_page) {
  const auto result = evaluateJavaScript(p_page, QStringLiteral(R"JS((() => {
    const visual = document.querySelector('#visual-map');
    const viewer = document.querySelector('#map-viewer');
    const map = document.querySelector('#mindmap');
    const viewport = document.querySelector('#map-viewport');
    const space = document.querySelector('#map-space');
    const controls = document.querySelector('#map-controls');
    const svg = map.querySelector('.m3-connectors');
    const image = map.querySelector('.m3-node-image');
    const articles = document.querySelector('#articles');
    const visible = element => {
      if (!element) return false;
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
    const deep = entries.find(entry =>
      entry.querySelector('.m3-article-topic').textContent === 'Deep café eight');
    const note = deep.querySelector('.m3-article-note');
    const paragraphs = document.querySelector('#m3-node-6e34 .m3-article-note');
    const figure = articles.querySelector('.m3-article-image');
    const nodes = Array.from(map.querySelectorAll('.m3-node'));
    const rootTopic = nodes[0].querySelector('.m3-topic');
    const styledNode = nodes.find(node => node.dataset.nodeId === 'n2');
    const styledTopic = styledNode.querySelector('.m3-topic');
    const topicStyle = getComputedStyle(styledTopic.firstElementChild || styledTopic);
    const interactive = viewer.dataset.mode === 'interactive';
    return JSON.stringify({
      mode:viewer.dataset.mode, mapVisible:visible(map), articlesVisible:visible(articles),
      controlsExist:!!controls, controlsVisible:visible(controls),
      controlsOutsideMap:!controls || (!map.contains(controls) && !viewport.contains(controls)),
      separatorVisible:visible(document.querySelector('#map-articles-separator')),
      decoded:image.complete && image.naturalWidth > 0,
      source:image.currentSrc, imageWidth:image.naturalWidth, imageHeight:image.naturalHeight,
      naturalWidth:Number(map.dataset.width), naturalHeight:Number(map.dataset.height),
      map:rect(map), mapViewport:rect(viewport), mapSpace:rect(space),
      visual:rect(visual), articles:rect(articles),
      topics:nodes.map(node => node.querySelector('.m3-topic').innerText.replace(/\s+/g, ' ').trim()),
      nodeIds:nodes.map(node => node.dataset.nodeId), rootTopic:rect(rootTopic),
      topicUserSelect:getComputedStyle(rootTopic).userSelect,
      topicAnchors:nodes.every(node => {
        const link = node.querySelector('.m3-topic');
        const target = document.getElementById(link.hash.slice(1));
        return link.tagName === 'A' && link.getAttribute('draggable') === 'false' && target &&
          target.querySelector('.m3-article-topic').innerText.replace(/\s+/g, ' ').trim() ===
            link.innerText.replace(/\s+/g, ' ').trim();
      }),
      rtlDirection:getComputedStyle(nodes.find(node => node.dataset.nodeId === 'n9')
        .querySelector('.m3-topic').firstElementChild).direction,
      svgNamespace:svg.namespaceURI, svgPointerEvents:getComputedStyle(svg).pointerEvents,
      treeEdges:svg.querySelectorAll('.m3-tree-edge').length,
      crossLinks:svg.querySelectorAll('.m3-cross-link').length,
      rasterSurfaces:map.querySelectorAll('canvas,foreignObject').length,
      geometryFits:Array.from(map.querySelectorAll('.m3-node,.m3-link-label,path,polygon'))
        .filter(visible).every(element => inside(rect(element), rect(map)) &&
          inside(rect(element), rect(interactive ? space : viewport))),
      textFits:nodes.every(node => {
        const range = document.createRange(); range.selectNodeContents(node.querySelector('.m3-topic'));
        return inside(range.getBoundingClientRect(), rect(node));
      }),
      styled:{fontSize:parseFloat(topicStyle.fontSize), weight:parseInt(topicStyle.fontWeight,10),
        italic:topicStyle.fontStyle, color:topicStyle.color,
        background:getComputedStyle(styledNode).backgroundColor},
      breakAfter:getComputedStyle(visual).breakAfter,
      pageBreakAfter:getComputedStyle(visual).pageBreakAfter,
      chromeHidden:!visible(document.querySelector('main > h1')),
      headings:entries.map(entry => entry.querySelector('.m3-article-topic')
        .innerText.replace(/\s+/g, ' ').trim()),
      articleVisibility:entries.map(visible), text:articles.innerText,
      links:Array.from(articles.querySelectorAll('a'), link => ({href:link.getAttribute('href'),
        targetExists:!link.hash || !!document.getElementById(decodeURIComponent(link.hash.slice(1)))})),
      references:Array.from(articles.querySelectorAll('.m3-cross-reference'), link => ({
        href:link.getAttribute('href'), text:link.textContent, title:link.title,
        owner:link.closest('article').id,
        inline:link.parentElement.classList.contains('m3-article-references') &&
          link.parentElement.previousElementSibling.classList.contains('m3-article-topic')})),
      tags:Array.from(articles.querySelectorAll('.m3-article-tag'), tag => ({text:tag.textContent,
        visible:visible(tag), rounded:parseFloat(getComputedStyle(tag).borderRadius) > 0})),
      icons:Array.from(articles.querySelectorAll('.m3-article-icon'), icon => ({text:icon.textContent,
        visible:visible(icon), inHeading:!!icon.closest('h3')})),
      figure:{decoded:figure.complete && figure.naturalWidth > 0, source:figure.currentSrc,
        naturalWidth:figure.naturalWidth, naturalHeight:figure.naturalHeight,
        bounds:rect(figure), fits:inside(rect(figure), rect(articles)), visible:visible(figure)},
      noteText:note.innerText, noteParagraphs:note.querySelectorAll('p').length,
      paragraphs:Array.from(paragraphs.querySelectorAll('p'), paragraph => paragraph.innerText),
      noteMarkup:paragraphs.querySelectorAll('b,script').length,
      deepHeading:rect(deep.querySelector('h3')), rootHeading:rect(entries[0].querySelector('h3')),
      note:rect(note), noteBreak:getComputedStyle(note).breakInside,
      noteFont:parseFloat(getComputedStyle(note).fontSize),
      headingBreak:getComputedStyle(deep.querySelector('h3')).breakAfter
    });
  })())JS"));
  return QJsonDocument::fromJson(result.toString().toUtf8()).object();
}

void verifyCompleteArticles(const QJsonObject &p_state, bool p_tall = false) {
  const auto headings = p_state.value(QStringLiteral("headings")).toArray();
  QCOMPARE(headings.size(), topics(p_tall).size());
  for (const auto &topic : topics(p_tall)) {
    QVERIFY2(headings.contains(topic.simplified()), qPrintable(topic));
  }
  QVERIFY(p_state.value(QStringLiteral("articlesVisible")).toBool());
  for (const auto &visible : p_state.value(QStringLiteral("articleVisibility")).toArray()) {
    QVERIFY(visible.toBool());
  }
  QCOMPARE(p_state.value(QStringLiteral("noteText")).toString().simplified(), longNote().simplified());
  QCOMPARE(p_state.value(QStringLiteral("noteParagraphs")).toInt(), 1);
  QCOMPARE(p_state.value(QStringLiteral("noteMarkup")).toInt(), 0);
  const auto paragraphs = p_state.value(QStringLiteral("paragraphs")).toArray();
  QCOMPARE(paragraphs.size(), 2);
  QCOMPARE(paragraphs[0].toString(), QStringLiteral("First paragraph <b>is plain text</b>\nsecond line"));
  QCOMPARE(paragraphs[1].toString(), QStringLiteral("Second paragraph & preserved"));
  bool externalLink = false;
  for (const auto &value : p_state.value(QStringLiteral("links")).toArray()) {
    const auto link = value.toObject();
    const auto href = link.value(QStringLiteral("href")).toString();
    QVERIFY(!href.startsWith(QStringLiteral("javascript:"), Qt::CaseInsensitive));
    if (href.startsWith(QLatin1Char('#'))) {
      QVERIFY(link.value(QStringLiteral("targetExists")).toBool());
    }
    externalLink |= href == QStringLiteral("https://example.invalid/deep?q=1&x=2");
  }
  QVERIFY(externalLink);
  const auto references = p_state.value(QStringLiteral("references")).toArray();
  QCOMPARE(references.size(), 1);
  const auto reference = references[0].toObject();
  QCOMPARE(reference.value(QStringLiteral("href")).toString(), QStringLiteral("#m3-node-6e39"));
  QCOMPARE(reference.value(QStringLiteral("owner")).toString(), QStringLiteral("m3-node-6e37"));
  QVERIFY(reference.value(QStringLiteral("text")).toString().contains(topics()[9]));
  QCOMPARE(reference.value(QStringLiteral("title")).toString(), QStringLiteral("Across the forest"));
  QVERIFY(reference.value(QStringLiteral("inline")).toBool());
  const auto tags = p_state.value(QStringLiteral("tags")).toArray();
  QCOMPARE(tags.size(), 2);
  for (const auto &value : tags) {
    const auto tag = value.toObject();
    QCOMPARE(tag.value(QStringLiteral("text")).toString(), QStringLiteral("Research <&>"));
    QVERIFY(tag.value(QStringLiteral("visible")).toBool());
    QVERIFY(tag.value(QStringLiteral("rounded")).toBool());
  }
  const auto icons = p_state.value(QStringLiteral("icons")).toArray();
  QCOMPARE(icons.size(), 2);
  for (const auto &value : icons) {
    const auto icon = value.toObject();
    QVERIFY(!icon.value(QStringLiteral("text")).toString().isEmpty());
    QVERIFY(icon.value(QStringLiteral("visible")).toBool());
    QVERIFY(icon.value(QStringLiteral("inHeading")).toBool());
  }
  const auto figure = p_state.value(QStringLiteral("figure")).toObject();
  QVERIFY(figure.value(QStringLiteral("decoded")).toBool());
  QVERIFY(figure.value(QStringLiteral("visible")).toBool());
  QVERIFY(figure.value(QStringLiteral("fits")).toBool());
  QCOMPARE(figure.value(QStringLiteral("source")).toString(), p_state.value(QStringLiteral("source")).toString());
  const auto bounds = figure.value(QStringLiteral("bounds")).toObject();
  const double ratio = bounds.value(QStringLiteral("width")).toDouble() /
                       bounds.value(QStringLiteral("height")).toDouble();
  QVERIFY(std::abs(ratio - 123.0 / 67.0) < 0.01);
}

void verifyMapDocument(const QJsonObject &p_state, bool p_tall = false) {
  const auto exportedTopics = p_state.value(QStringLiteral("topics")).toArray();
  QCOMPARE(exportedTopics.size(), topics(p_tall).size());
  for (const auto &topic : topics(p_tall)) {
    QVERIFY2(exportedTopics.contains(topic.simplified()), qPrintable(topic));
  }
  QCOMPARE(p_state.value(QStringLiteral("svgNamespace")).toString(),
           QStringLiteral("http://www.w3.org/2000/svg"));
  QCOMPARE(p_state.value(QStringLiteral("treeEdges")).toInt(), topics(p_tall).size() - 2);
  QCOMPARE(p_state.value(QStringLiteral("crossLinks")).toInt(), 1);
  QCOMPARE(p_state.value(QStringLiteral("rasterSurfaces")).toInt(), 0);
  QCOMPARE(p_state.value(QStringLiteral("svgPointerEvents")).toString(), QStringLiteral("none"));
  QVERIFY(p_state.value(QStringLiteral("topicUserSelect")).toString() != QStringLiteral("none"));
  QCOMPARE(p_state.value(QStringLiteral("rtlDirection")).toString(), QStringLiteral("rtl"));
  QVERIFY(p_state.value(QStringLiteral("topicAnchors")).toBool());
  QVERIFY2(p_state.value(QStringLiteral("geometryFits")).toBool(),
           "All geometry must fit the map/spacer, and the viewport only in static mode");
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
QJsonArray readPdfPages(const QString &p_file) {
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
  for (let number = 1; number <= pdf.numPages; ++number) {
    const page = await pdf.getPage(number);
    const content = await page.getTextContent();
    const operators = await page.getOperatorList();
    const images = [], stack = [];
    let matrix = [1,0,0,1,0,0];
    for (let index = 0; index < operators.fnArray.length; ++index) {
      const operation = operators.fnArray[index], args = operators.argsArray[index];
      if (operation === pdfjs.OPS.save) stack.push(matrix.slice());
      else if (operation === pdfjs.OPS.restore) matrix = stack.pop();
      else if (operation === pdfjs.OPS.transform) matrix = pdfjs.Util.transform(matrix, args);
      else if (operation === pdfjs.OPS.paintImageXObject ||
               operation === pdfjs.OPS.paintInlineImageXObject) {
        images.push({width:Math.hypot(matrix[0], matrix[1]),
                     height:Math.hypot(matrix[2], matrix[3])});
      }
    }
    const viewport = page.getViewport({scale:1});
    pages.push({items:content.items.map(item => ({text:item.str, height:item.height})),
                images:images, width:viewport.width, height:viewport.height});
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
  void standaloneScrollableViewerAndReadableArticles_data();
  void standaloneScrollableViewerAndReadableArticles();
  void printableNativeForest_data();
  void printableNativeForest();
  void customShowsCompleteStaticDocument_data();
  void customShowsCompleteStaticDocument();
  void largeHtmlAndLocalSidecarProducePdf();
  void invalidInputsLeaveNoOutput();
  void delayedImageSettlesBeforePdf();
  void cancellationDiscardsLateResponseAndConverterCanBeReused();
};

void TestHtmlPdfExporter::standaloneScrollableViewerAndReadableArticles_data() {
  QTest::addColumn<bool>("tall");
  QTest::newRow("wide-map") << false;
  QTest::newRow("tall-map") << true;
}

void TestHtmlPdfExporter::standaloneScrollableViewerAndReadableArticles() {
  QFETCH(bool, tall);
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  QVERIFY(writeFile(directory.filePath(QStringLiteral("fixture.png")), imageBytes()));
  ServiceLocator services;
  MindMapExporter renderer(services, nullptr, nullptr);
  const auto html = directory.filePath(QStringLiteral("standalone.html"));
  QVERIFY(renderer.renderHtml(nativeInput(directory.path(), tall), ExportHtmlRequest(), html));

  BrowserPage browser;
  QVERIFY(browser.profile.isOffTheRecord());
  QVERIFY(loadFile(browser.page, html));
  auto state = documentState(browser.page);
  QVERIFY(!state.isEmpty());
  QCOMPARE(state.value(QStringLiteral("mode")).toString(), QStringLiteral("interactive"));
  QVERIFY(state.value(QStringLiteral("mapVisible")).toBool());
  QVERIFY(state.value(QStringLiteral("controlsVisible")).toBool());
  QVERIFY(state.value(QStringLiteral("controlsOutsideMap")).toBool());
  QVERIFY(state.value(QStringLiteral("separatorVisible")).toBool());
  QVERIFY(state.value(QStringLiteral("articles")).toObject().value(QStringLiteral("top")).toDouble() >=
          state.value(QStringLiteral("visual")).toObject().value(QStringLiteral("bottom")).toDouble());
  verifyMapDocument(state, tall);
  verifyCompleteArticles(state, tall);
  auto viewer = viewerState(browser.page);
  QVERIFY(std::abs(viewer.value(QStringLiteral("scale")).toDouble() - 1) < 0.001);
  QCOMPARE(viewer.value(QStringLiteral("zoomText")).toString(), QStringLiteral("100%"));
  QCOMPARE(viewer.value(QStringLiteral("overflow")).toString(), QStringLiteral("auto"));
  QVERIFY(viewer.value(QStringLiteral("clientHeight")).toDouble() <= 900 * 0.7 + 1);
  QVERIFY(viewer.value(QStringLiteral("maxScrollLeft")).toDouble() > 100);
  if (tall) {
    QVERIFY(viewer.value(QStringLiteral("maxScrollTop")).toDouble() > 100);
  }
  QVERIFY(viewer.value(QStringLiteral("rootVisible")).toBool());
  const auto rootTopic = state.value(QStringLiteral("rootTopic")).toObject();
  const QPoint wordPoint(qRound(rootTopic.value(QStringLiteral("left")).toDouble() + 4),
                         qRound(rootTopic.value(QStringLiteral("top")).toDouble() + 7));
  QVERIFY2(evaluateJavaScript(browser.page,
                              QStringLiteral("!!document.elementFromPoint(%1,%2).closest('.m3-topic')")
                                  .arg(wordPoint.x()).arg(wordPoint.y())).toBool(),
           "Connectors and overlay controls must not intercept node text pointer hits");
  const auto selected = evaluateJavaScript(browser.page, QStringLiteral(R"JS((() => {
    const range = document.createRange();
    range.selectNodeContents(document.querySelector('#mindmap .m3-topic'));
    const selection = window.getSelection();
    selection.removeAllRanges(); selection.addRange(range);
    const text = selection.toString(); selection.removeAllRanges();
    return text;
  })())JS")).toString();
  QCOMPARE(selected.simplified(), topics().first().simplified());

  // Work from the interior, rather than the initial root position at a clamped scroll edge.
  QVERIFY(evaluateJavaScript(browser.page, QStringLiteral(R"JS((() => {
    const viewport = document.getElementById('map-viewport');
    viewport.scrollLeft = (viewport.scrollWidth - viewport.clientWidth) / 2;
    viewport.scrollTop = (viewport.scrollHeight - viewport.clientHeight) / 2;
    return true;
  })())JS")).toBool());
  auto before = viewerState(browser.page);
  QVERIFY(clickElement(browser, QStringLiteral("#map-zoom-in")));
  QTRY_VERIFY_WITH_TIMEOUT(std::abs(viewerState(browser.page).value(QStringLiteral("scale"))
                                       .toDouble() - 1.25) < 0.001, 5000);
  viewer = viewerState(browser.page);
  verifyCenterPreserved(before, viewer);
  QVERIFY(viewer.value(QStringLiteral("controlsOverlay")).toBool());
  QVERIFY(std::abs(viewer.value(QStringLiteral("controlTop")).toDouble() -
                   before.value(QStringLiteral("controlTop")).toDouble()) < 1);
  QVERIFY(std::abs(viewer.value(QStringLiteral("controlWidth")).toDouble() -
                   before.value(QStringLiteral("controlWidth")).toDouble()) < 1);
  QCOMPARE(viewer.value(QStringLiteral("zoomText")).toString(), QStringLiteral("125%"));
  before = viewer;
  QVERIFY(activateControl(browser.page, QStringLiteral("map-zoom-out")));
  viewer = viewerState(browser.page);
  QVERIFY(std::abs(viewer.value(QStringLiteral("scale")).toDouble() - 1) < 0.001);
  verifyCenterPreserved(before, viewer);

  QVERIFY(activateControl(browser.page, QStringLiteral("map-zoom-in"), 3));
  before = viewerState(browser.page);
  browser.view.resize(760, 720);
  QTRY_VERIFY_WITH_TIMEOUT(viewerState(browser.page).value(QStringLiteral("clientWidth")).toDouble() <
                              before.value(QStringLiteral("clientWidth")).toDouble(), 5000);
  QVERIFY(evaluateJavaScript(browser.page,
                             QStringLiteral("dispatchEvent(new Event('resize')); true")).toBool());
  QVERIFY(settleLayout(browser.page));
  viewer = viewerState(browser.page);
  QVERIFY(std::abs(viewer.value(QStringLiteral("scale")).toDouble() -
                   before.value(QStringLiteral("scale")).toDouble()) < 0.001);
  verifyCenterPreserved(before, viewer);
  before = viewer;
  QVERIFY(activateControl(browser.page, QStringLiteral("map-reset")));
  viewer = viewerState(browser.page);
  QVERIFY(std::abs(viewer.value(QStringLiteral("scale")).toDouble() - 1) < 0.001);
  QCOMPARE(viewer.value(QStringLiteral("zoomText")).toString(), QStringLiteral("100%"));
  verifyCenterPreserved(before, viewer);

  QVERIFY(activateControl(browser.page, QStringLiteral("map-zoom-out"), 40));
  viewer = viewerState(browser.page);
  QVERIFY(std::abs(viewer.value(QStringLiteral("scale")).toDouble() - 0.1) < 0.001);
  QCOMPARE(viewer.value(QStringLiteral("zoomText")).toString(), QStringLiteral("10%"));
  QVERIFY(viewer.value(QStringLiteral("outDisabled")).toBool());
  QVERIFY(!viewer.value(QStringLiteral("inDisabled")).toBool());
  QVERIFY(std::abs(viewer.value(QStringLiteral("insetX")).toDouble() -
                   (viewer.value(QStringLiteral("clientWidth")).toDouble() -
                    viewer.value(QStringLiteral("mapWidth")).toDouble()) / 2) < 1);
  QVERIFY(std::abs(viewer.value(QStringLiteral("insetY")).toDouble() -
                   (viewer.value(QStringLiteral("clientHeight")).toDouble() -
                    viewer.value(QStringLiteral("mapHeight")).toDouble()) / 2) < 1);
  QVERIFY(activateControl(browser.page, QStringLiteral("map-zoom-in"), 40));
  viewer = viewerState(browser.page);
  QVERIFY(std::abs(viewer.value(QStringLiteral("scale")).toDouble() - 4) < 0.001);
  QCOMPARE(viewer.value(QStringLiteral("zoomText")).toString(), QStringLiteral("400%"));
  QVERIFY(viewer.value(QStringLiteral("inDisabled")).toBool());
  QVERIFY(!viewer.value(QStringLiteral("outDisabled")).toBool());
  QVERIFY(activateControl(browser.page, QStringLiteral("map-zoom-out")));
  viewer = viewerState(browser.page);
  QVERIFY(std::abs(viewer.value(QStringLiteral("scale")).toDouble() - 3.2) < 0.001);
  QVERIFY(!viewer.value(QStringLiteral("inDisabled")).toBool());

  // Main Node moves only the map scroller and keeps the selected zoom and document anchor.
  QVERIFY(activateControl(browser.page, QStringLiteral("map-reset")));
  QVERIFY(activateControl(browser.page, QStringLiteral("map-zoom-in")));
  QVERIFY(evaluateJavaScript(browser.page, QStringLiteral(R"JS((() => {
    const viewport = document.getElementById('map-viewport');
    viewport.scrollLeft = viewport.scrollWidth; viewport.scrollTop = viewport.scrollHeight;
    return true;
  })())JS")).toBool());
  before = viewerState(browser.page);
  QVERIFY(!before.value(QStringLiteral("rootVisible")).toBool());
  QVERIFY(activateControl(browser.page, QStringLiteral("map-main-node")));
  viewer = viewerState(browser.page);
  QVERIFY(viewer.value(QStringLiteral("rootVisible")).toBool());
  QCOMPARE(viewer.value(QStringLiteral("scale")).toDouble(), before.value(QStringLiteral("scale")).toDouble());
  before.insert(QStringLiteral("centerX"), viewer.value(QStringLiteral("rootX")));
  before.insert(QStringLiteral("centerY"), viewer.value(QStringLiteral("rootY")));
  verifyCenterPreserved(before, viewer);
  QVERIFY(evaluateJavaScript(browser.page, QStringLiteral("location.hash === ''")).toBool());

  // Real pointer input must follow the topic anchor and then the authored inline reference.
  QVERIFY(clickElement(browser, QStringLiteral("#mindmap [data-node-id='n7'] .m3-topic")));
  QTRY_COMPARE_WITH_TIMEOUT(evaluateJavaScript(browser.page, QStringLiteral("location.hash")).toString(),
                            QStringLiteral("#m3-node-6e37"), 5000);
  QVERIFY(evaluateJavaScript(browser.page, QStringLiteral(R"JS((() => {
    const heading = document.querySelector('#m3-node-6e37 .m3-article-topic').getBoundingClientRect();
    return heading.top >= 0 && heading.bottom <= innerHeight;
  })())JS")).toBool());
  QVERIFY(clickElement(browser, QStringLiteral("#m3-node-6e37 .m3-cross-reference")));
  QTRY_COMPARE_WITH_TIMEOUT(evaluateJavaScript(browser.page, QStringLiteral("location.hash")).toString(),
                            QStringLiteral("#m3-node-6e39"), 5000);
  QVERIFY(evaluateJavaScript(browser.page, QStringLiteral(R"JS((() => {
    const heading = document.querySelector('#m3-node-6e39 .m3-article-topic').getBoundingClientRect();
    return heading.top >= 0 && heading.bottom <= innerHeight;
  })())JS")).toBool());
  state = documentState(browser.page);
  QVERIFY(state.value(QStringLiteral("mapVisible")).toBool());
  verifyCompleteArticles(state, tall);
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
  verifyStaticViewer(state, viewerState(browser.page));
  QVERIFY(!state.value(QStringLiteral("separatorVisible")).toBool());
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
  const double expectedScale = qMin(1.0, qMin((printable.width() - 1) * pixelsPerMillimeter /
                                                state.value(QStringLiteral("naturalWidth")).toDouble(),
                                            (printable.height() - 1) * pixelsPerMillimeter /
                                                state.value(QStringLiteral("naturalHeight")).toDouble()));
  QVERIFY(std::abs(width / state.value(QStringLiteral("naturalWidth")).toDouble() - expectedScale) < 0.001);
  const auto figure = state.value(QStringLiteral("figure")).toObject()
                          .value(QStringLiteral("bounds")).toObject();
  QVERIFY(figure.value(QStringLiteral("width")).toDouble() <= printable.width() * pixelsPerMillimeter);
  QVERIFY(figure.value(QStringLiteral("height")).toDouble() <= printable.height() * pixelsPerMillimeter);
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

  // Static resize must not override paper geometry or apply a second layout zoom.
  browser.view.resize(400, 700);
  QVERIFY(
      evaluateJavaScript(browser.page, QStringLiteral("dispatchEvent(new Event('resize')); true"))
          .toBool());
  QVERIFY(settleLayout(browser.page));
  const auto resized = documentState(browser.page);
  verifyMapDocument(resized);
  const auto resizedMap = resized.value(QStringLiteral("map")).toObject();
  QVERIFY(std::abs(resizedMap.value(QStringLiteral("width")).toDouble() - width) < 1);
  QVERIFY(std::abs(resizedMap.value(QStringLiteral("height")).toDouble() - height) < 1);
  verifyStaticViewer(resized, viewerState(browser.page));
  QVERIFY(evaluateJavaScript(browser.page, QStringLiteral(R"JS((() => {
    for (const id of ['map-viewer', 'map-viewport', 'map-space']) {
      const element = document.getElementById(id);
      element.scrollLeft = 100; element.scrollTop = 100;
      if (element.scrollLeft !== 0 || element.scrollTop !== 0) return false;
    }
    return true;
  })())JS")).toBool());

  HtmlPdfExporter converter(services);
  connect(&converter, &HtmlPdfExporter::logRequested, this,
          [](const QString &p_message) { qInfo().noquote() << p_message; });
  const auto pdf = directory.filePath(QStringLiteral("forest.pdf"));
  QVERIFY(converter.exportFiles(request.options.m_pdfOption, {html}, pdf));
  verifyPdf(pdf);
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
  const auto pages = readPdfPages(pdf);
  QVERIFY2(pages.size() >= 2, "The PDF reader must expose the map and every article page");
  QStringList pageText;
  bool readableBody = false;
  int articleImages = 0;
  for (int index = 0; index < pages.size(); ++index) {
    const auto page = pages[index].toObject();
    QCOMPARE(page.value(QStringLiteral("width")).toDouble() >
                 page.value(QStringLiteral("height")).toDouble(), landscape);
    QString text;
    for (const auto &item : page.value(QStringLiteral("items")).toArray()) {
      const auto entry = item.toObject();
      const auto line = entry.value(QStringLiteral("text")).toString();
      text += line + QLatin1Char(' ');
      if (index > 0 && line.contains(QStringLiteral("Paragraph "))) {
        const double height = entry.value(QStringLiteral("height")).toDouble();
        QVERIFY2(std::abs(height - 12.0) < 0.1,
                 qPrintable(QStringLiteral("Map overflow/zoom shrank body text to %1 pt on page %2")
                                .arg(height).arg(index + 1)));
        readableBody = true;
      }
    }
    pageText.append(text.simplified());
    const auto images = page.value(QStringLiteral("images")).toArray();
    if (index == 0) {
      QVERIFY2(!images.isEmpty(), "The complete first-page map must retain its embedded image");
    } else {
      articleImages += images.size();
    }
    for (const auto &value : images) {
      const auto image = value.toObject();
      const double imageWidth = image.value(QStringLiteral("width")).toDouble();
      const double imageHeight = image.value(QStringLiteral("height")).toDouble();
      QVERIFY(imageWidth > 0 && imageHeight > 0);
      // Chromium rounds layout-zoomed image extents to CSS pixels before printing. Bound the
      // error in PDF points rather than a ratio that amplifies rounding on a small fitted map.
      QVERIFY(std::abs(imageWidth - imageHeight * 123.0 / 67.0) < 1.0);
      QVERIFY(imageWidth <= printable.width() * 72.0 / 25.4 + 1);
      QVERIFY(imageHeight <= printable.height() * 72.0 / 25.4 + 1);
    }
  }
  const auto firstPage = pageText.first();
  QVERIFY(firstPage.contains(QStringLiteral("Root")));
  for (const auto &level : {QStringLiteral("two"), QStringLiteral("three"), QStringLiteral("four"),
                            QStringLiteral("five"), QStringLiteral("six"), QStringLiteral("seven")}) {
    QVERIFY2(firstPage.contains(QStringLiteral("Level ") + level), qPrintable(firstPage));
  }
  QVERIFY(firstPage.contains(QStringLiteral("Deep")));
  QVERIFY(firstPage.contains(QStringLiteral("Separate orphan tree")));
  QVERIFY(!firstPage.contains(QStringLiteral("LONG_NOTE_BEGIN")));
  QVERIFY(!firstPage.contains(QStringLiteral("First paragraph")));
  QVERIFY2(pageText[1].contains(QStringLiteral("Root")) &&
               pageText[1].contains(QStringLiteral("Research")),
           "Articles must start with the root topic and its readable badges on page 2");
  QVERIFY(pageText[1].indexOf(QStringLiteral("Root")) <
          pageText[1].indexOf(QStringLiteral("Level two")));
  QVERIFY(readableBody);
  QVERIFY(articleImages > 0);
  const auto articleText = pageText.mid(1).join(QLatin1Char(' '));
  QVERIFY(articleText.contains(QStringLiteral("LONG_NOTE_BEGIN")));
  QVERIFY(articleText.contains(QStringLiteral("LONG_NOTE_END")));
  for (int index = 0; index < 96; ++index) {
    QVERIFY2(articleText.contains(QStringLiteral("Paragraph %1:").arg(index)),
             qPrintable(QStringLiteral("Missing long-note paragraph %1").arg(index)));
  }
#endif
}

void TestHtmlPdfExporter::customShowsCompleteStaticDocument_data() {
  QTest::addColumn<bool>("tall");
  QTest::newRow("wide-map") << false;
  QTest::newRow("tall-map") << true;
}

void TestHtmlPdfExporter::customShowsCompleteStaticDocument() {
  QFETCH(bool, tall);
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
  QVERIFY(renderer.renderHtml(nativeInput(directory.path(), tall), request, html));
  BrowserPage browser;
  browser.view.resize(1200, 400);
  QVERIFY(loadFile(browser.page, html));
  auto state = documentState(browser.page);
  QVERIFY(!state.isEmpty());
  verifyMapDocument(state, tall);
  verifyCompleteArticles(state, tall);
  auto viewer = viewerState(browser.page);
  verifyStaticViewer(state, viewer);
  QVERIFY(state.value(QStringLiteral("mapVisible")).toBool());
  QVERIFY(!state.value(QStringLiteral("chromeHidden")).toBool());
  QVERIFY(state.value(QStringLiteral("separatorVisible")).toBool());
  QCOMPARE(state.value(QStringLiteral("breakAfter")).toString(), QStringLiteral("auto"));
  QCOMPARE(state.value(QStringLiteral("pageBreakAfter")).toString(), QStringLiteral("auto"));
  QVERIFY(state.value(QStringLiteral("articles")).toObject().value(QStringLiteral("top")).toDouble() >=
          state.value(QStringLiteral("visual")).toObject().value(QStringLiteral("bottom")).toDouble());
  if (tall) {
    QVERIFY2(viewer.value(QStringLiteral("clientHeight")).toDouble() > 400 * 0.7,
             "A static map must not inherit the interactive viewport height cap");
  }
  const double originalWidth = viewer.value(QStringLiteral("mapWidth")).toDouble();
  browser.view.resize(640, 700);
  QTRY_VERIFY_WITH_TIMEOUT(viewerState(browser.page).value(QStringLiteral("mapWidth")).toDouble() <
                              originalWidth - 100, 5000);
  QVERIFY(settleLayout(browser.page));
  state = documentState(browser.page);
  viewer = viewerState(browser.page);
  verifyMapDocument(state, tall);
  verifyCompleteArticles(state, tall);
  verifyStaticViewer(state, viewer);
  QVERIFY(viewer.value(QStringLiteral("mapWidth")).toDouble() <= 640);
  const double ratio = state.value(QStringLiteral("naturalWidth")).toDouble() /
                       state.value(QStringLiteral("naturalHeight")).toDouble();
  QVERIFY(std::abs(viewer.value(QStringLiteral("mapWidth")).toDouble() /
                       viewer.value(QStringLiteral("mapHeight")).toDouble() - ratio) < 0.01);
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
