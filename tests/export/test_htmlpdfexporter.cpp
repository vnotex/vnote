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
      node.insert(QStringLiteral("tags"), QJsonArray{QStringLiteral("Research <&>"), QString(),
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
      node.insert(QStringLiteral("icons"), QJsonArray{QStringLiteral("flag")});
      node.insert(QStringLiteral("tags"), QJsonArray{QStringLiteral("Deep research")});
      node.insert(QStringLiteral("image"),
                  QJsonObject{{QStringLiteral("url"), QStringLiteral("fixture.png")},
                              {QStringLiteral("width"), 123},
                              {QStringLiteral("height"), 67}});
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
  })())JS"))
           .toBool()) {
    return false;
  }
  const QPointer<QWebEnginePage> page(&p_page);
  return QTest::qWaitFor(
      [page] {
        return page &&
               evaluateJavaScript(*page, QStringLiteral("window.testLayoutSettled === true"))
                   .toBool();
      },
      5000);
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
               return page && evaluateJavaScript(
                                  *page, QStringLiteral("window.testResourcesReady === true"))
                                  .toBool();
             },
             15000) &&
         settleLayout(p_page);
}

struct BrowserPage {
  QWebEngineProfile profile;
  QWebEnginePage page{&profile};
  QWebEngineView view;

  BrowserPage() {
    page.settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    page.settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
    view.setPage(&page);
    view.resize(1200, 900);
    view.show();
  }
};

bool clickElement(BrowserPage &p_browser, const QString &p_selector) {
  const auto selector =
      QString::fromUtf8(QJsonDocument(QJsonArray{p_selector}).toJson(QJsonDocument::Compact));
  const auto point = evaluateJavaScript(p_browser.page, QStringLiteral(R"JS((() => {
    const element = document.querySelector(%1[0]);
    if (!element) return null;
    element.scrollIntoView({block:'center', inline:'center'});
    const rect = element.getBoundingClientRect();
    const x = rect.left + rect.width / 2, y = rect.top + rect.height / 2;
    const hit = document.elementFromPoint(x, y);
    return hit && (hit === element || element.contains(hit)) ? [x,y] : null;
  })())JS")
                                                            .arg(selector))
                         .toList();
  if (point.size() != 2 || !p_browser.view.focusProxy()) {
    return false;
  }
  const QPoint position(qRound(point[0].toDouble()), qRound(point[1].toDouble()));
  auto *target = p_browser.view.focusProxy();
  QTest::mouseClick(target, Qt::LeftButton, Qt::NoModifier,
                    target->mapFrom(&p_browser.view, position));
  return settleLayout(p_browser.page);
}

QJsonObject documentState(QWebEnginePage &p_page) {
  const auto result = evaluateJavaScript(p_page, QStringLiteral(R"JS((() => {
    const visual = document.querySelector('#visual-map');
    const map = document.querySelector('#mindmap');
    const svg = map.querySelector('.m3-connectors');
    const image = map.querySelector('.m3-node-image');
    const articles = document.querySelector('#articles');
    const main = document.querySelector('main');
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
    const unscaled = element => {
      const style = getComputedStyle(element);
      return style.transform === 'none' && (style.zoom === 'normal' || Number(style.zoom) === 1);
    };
    const unclipped = element => {
      const style = getComputedStyle(element);
      return style.overflowX === 'visible' && style.overflowY === 'visible';
    };
    const ancestors = [];
    for (let element = map.parentElement; element; element = element.parentElement) {
      ancestors.push(element);
    }
    const entries = Array.from(articles.querySelectorAll('article'));
    const deep = document.getElementById('m3-node-6e37');
    const note = deep.querySelector('.m3-article-note');
    const paragraphs = document.querySelector('#m3-node-6e34 .m3-article-note');
    const figures = Array.from(articles.querySelectorAll('.m3-article-image'));
    const nodes = Array.from(map.querySelectorAll('.m3-node'));
    const rootTopic = nodes[0].querySelector('.m3-topic');
    const styledNode = nodes.find(node => node.dataset.nodeId === 'n2');
    const styledTopic = styledNode.querySelector('.m3-topic');
    const topicStyle = getComputedStyle(styledTopic.firstElementChild || styledTopic);
    const lists = Array.from(articles.querySelectorAll('ul'));
    const listItems = Array.from(articles.querySelectorAll('li'));
    const rows = Array.from(deep.children);
    const safeUrl = deep.querySelector('.m3-article-url');
    const urlLink = safeUrl.querySelector('a');
    const urlIcon = safeUrl.querySelector('.m3-article-url-icon');
    const unsafeUrl = document.querySelector('#m3-node-6e34 .m3-article-url');
    const measure = document.createElement('span');
    measure.style.cssText = 'display:block;position:absolute;width:72ch;height:0;visibility:hidden';
    articles.appendChild(measure);
    const oldColumnWidth = measure.getBoundingClientRect().width;
    measure.remove();
    return JSON.stringify({
      inlineHandlers:Array.from(document.querySelectorAll('*')).some(element =>
        Array.from(element.attributes).some(attribute => /^on/i.test(attribute.name))),
      directMap:map.parentElement === visual && visual.children.length === 1,
      mapVisible:visible(map), articlesVisible:visible(articles),
      separatorVisible:visible(document.querySelector('#map-articles-separator')),
      decoded:image.complete && image.naturalWidth > 0,
      source:image.currentSrc, imageWidth:image.naturalWidth, imageHeight:image.naturalHeight,
      naturalWidth:Number(map.dataset.width), naturalHeight:Number(map.dataset.height),
      map:rect(map), visual:rect(visual), main:rect(main), articles:rect(articles),
      viewportWidth:document.documentElement.clientWidth, viewportHeight:innerHeight,
      documentWidth:document.scrollingElement.scrollWidth,
      documentHeight:document.scrollingElement.scrollHeight, scrollX:scrollX, scrollY:scrollY,
      oldColumnWidth:oldColumnWidth,
      uncapped:[document.body, main, articles].every(element => getComputedStyle(element).maxWidth === 'none'),
      naturalOverflow:[map, articles].concat(ancestors).every(unclipped),
      ancestorsUnscaled:ancestors.every(unscaled), articlesUnscaled:unscaled(articles),
      mapTransform:getComputedStyle(map).transform, mapZoom:getComputedStyle(map).zoom,
      mapPosition:getComputedStyle(map).position, visualPosition:getComputedStyle(visual).position,
      visualOverflow:getComputedStyle(visual).overflow,
      bodyFont:parseFloat(getComputedStyle(document.body).fontSize),
      nodeIds:nodes.map(node => node.dataset.nodeId), rootTopic:rect(rootTopic),
      topicUserSelect:getComputedStyle(rootTopic).userSelect,
      topicAnchors:nodes.every(node => {
        const link = node.querySelector('.m3-topic');
        const target = document.getElementById(link.hash.slice(1));
        const anchor = 'm3-node-' + Array.from(new TextEncoder().encode(node.dataset.nodeId),
          byte => byte.toString(16).padStart(2, '0')).join('');
        return link.tagName === 'A' && link.getAttribute('draggable') === 'false' && target &&
          target.tagName === 'ARTICLE' && target.id === anchor && articles.contains(target);
      }),
      rtlDirection:getComputedStyle(nodes.find(node => node.dataset.nodeId === 'n9')
        .querySelector('.m3-topic').firstElementChild).direction,
      svgNamespace:svg.namespaceURI, svgPointerEvents:getComputedStyle(svg).pointerEvents,
      treeEdges:svg.querySelectorAll('.m3-tree-edge').length,
      crossLinks:svg.querySelectorAll('.m3-cross-link').length,
      rasterSurfaces:map.querySelectorAll('canvas,foreignObject').length,
      geometryFits:[svg].concat(Array.from(map.querySelectorAll('.m3-node,.m3-link-label,path,polygon')))
        .filter(visible).every(element => inside(rect(element), rect(map))),
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
      headingTags:entries.map(entry => {
        const heading = entry.querySelector('.m3-article-title');
        let listDepth = 0;
        for (let parent = entry.parentElement; parent !== articles; parent = parent.parentElement) {
          if (parent.tagName === 'UL') ++listDepth;
        }
        return {id:entry.id, tag:heading.tagName, fontSize:parseFloat(getComputedStyle(heading).fontSize),
          flat:entry.parentElement === articles, listDepth:listDepth,
          titleOnly:heading.children.length === 1 && heading.firstElementChild.matches('.m3-article-topic') &&
            heading.textContent === heading.firstElementChild.textContent};
      }),
      rootHeadingCount:articles.querySelectorAll('h1').length,
      listStructure:lists.every(list =>
        (list.parentElement === articles && list.previousElementSibling.matches('article') &&
          list.previousElementSibling.querySelector('.m3-article-title').tagName === 'H6') ||
        (list.parentElement.tagName === 'LI' && list.previousElementSibling.matches('article'))) &&
        listItems.every(item => item.parentElement.tagName === 'UL' && item.firstElementChild.matches('article')),
      listsUnclipped:lists.concat(listItems).every(unclipped),
      listMarkers:listItems.map(item => {
        const style = getComputedStyle(item), marker = getComputedStyle(item, '::marker');
        const title = item.firstElementChild.querySelector('.m3-article-title');
        return {display:style.display, type:style.listStyleType, position:style.listStylePosition,
          markerFont:parseFloat(marker.fontSize), markerColor:marker.color,
          gutter:rect(title).left - rect(articles).left, title:rect(title), item:rect(item),
          clipped:!inside(rect(title), rect(articles)),
          parentPadding:parseFloat(getComputedStyle(item.parentElement).paddingLeft)};
      }),
      articleVisibility:entries.map(visible), text:articles.innerText,
      links:Array.from(articles.querySelectorAll('a'), link => ({href:link.getAttribute('href'),
        targetExists:!link.hash || !!document.getElementById(decodeURIComponent(link.hash.slice(1)))})),
      references:Array.from(articles.querySelectorAll('.m3-cross-reference'), link => ({
        href:link.getAttribute('href'), text:link.textContent, title:link.title,
        owner:link.closest('article').id,
        ownRow:link.parentElement.matches('div.m3-article-references') &&
          link.parentElement.parentElement === link.closest('article'),
        inTitle:!!link.closest('.m3-article-title')})),
      tags:Array.from(articles.querySelectorAll('.m3-article-tag'), tag => ({text:tag.textContent,
        visible:visible(tag), rounded:parseFloat(getComputedStyle(tag).borderRadius) > 0})),
      icons:Array.from(articles.querySelectorAll('.m3-article-icon'), icon => ({text:icon.textContent,
        visible:visible(icon), inHeading:!!icon.closest('.m3-article-title,h1,h2,h3,h4,h5,h6'),
        ownRow:icon.parentElement.matches('div.m3-article-icons') &&
          icon.parentElement.parentElement === icon.closest('article')})),
      rows:rows.map(row => ({classes:row.className, tag:row.tagName, bounds:rect(row), visible:visible(row)})),
      emptyRows:articles.querySelectorAll('.m3-article-icons,.m3-article-tags,.m3-article-url,.m3-article-references').length &&
        Array.from(articles.querySelectorAll('.m3-article-icons,.m3-article-tags,.m3-article-url,.m3-article-references'))
          .some(row => !row.textContent.trim()),
      sparseRows:document.querySelector('#m3-node-6e31').children.length,
      safeUrl:{tag:safeUrl.tagName, href:urlLink.getAttribute('href'),
        text:safeUrl.querySelector('.m3-article-url-text').textContent,
        icon:urlIcon.textContent, decorative:urlIcon.getAttribute('aria-hidden') === 'true',
        order:urlLink.children.length === 2 && urlLink.children[0] === urlIcon &&
          urlLink.children[1].matches('.m3-article-url-text')},
      unsafeUrl:{links:unsafeUrl.querySelectorAll('a').length,
        icons:unsafeUrl.querySelectorAll('.m3-article-url-icon').length,
        text:unsafeUrl.querySelector('.m3-article-url-text').textContent},
      figures:figures.map(figure => ({decoded:figure.complete && figure.naturalWidth > 0, source:figure.currentSrc,
        bounds:rect(figure), fits:inside(rect(figure), rect(articles)), visible:visible(figure)})),
      noteText:note.innerText, noteParagraphs:note.querySelectorAll('p').length,
      paragraphs:Array.from(paragraphs.querySelectorAll('p'), paragraph => paragraph.innerText),
      noteMarkup:paragraphs.querySelectorAll('b,script').length,
      note:rect(note), noteBreak:getComputedStyle(note).breakInside,
      noteFont:parseFloat(getComputedStyle(note).fontSize),
      headingBreak:getComputedStyle(deep.querySelector('.m3-article-title')).breakAfter
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
  QCOMPARE(p_state.value(QStringLiteral("rootHeadingCount")).toInt(), 2);
  const auto headingTags = p_state.value(QStringLiteral("headingTags")).toArray();
  QCOMPARE(headingTags.size(), headings.size());
  const double bodyFont = p_state.value(QStringLiteral("bodyFont")).toDouble();
  QVERIFY(bodyFont > 0);
  for (const auto &value : headingTags) {
    const auto heading = value.toObject();
    const auto id = heading.value(QStringLiteral("id")).toString();
    const auto node = QString::fromUtf8(QByteArray::fromHex(id.mid(8).toLatin1()));
    bool validIndex = false;
    const int index = node.mid(1).toInt(&validIndex);
    QVERIFY(validIndex && index >= 0 && index < topics(p_tall).size());
    const int depth = index < 8 ? index + 1 : (index == 8 ? 1 : 2);
    QCOMPARE(heading.value(QStringLiteral("tag")).toString(),
             depth <= 6 ? QStringLiteral("H%1").arg(depth) : QStringLiteral("P"));
    QCOMPARE(heading.value(QStringLiteral("flat")).toBool(), depth <= 6);
    QCOMPARE(heading.value(QStringLiteral("listDepth")).toInt(), qMax(0, depth - 6));
    QVERIFY(heading.value(QStringLiteral("titleOnly")).toBool());
    const double fontSize = heading.value(QStringLiteral("fontSize")).toDouble();
    QVERIFY(fontSize >= bodyFont - 0.01);
    if (depth <= 3) {
      const double factor = depth == 1 ? 2.0 : (depth == 2 ? 1.5 : 1.17);
      QVERIFY(std::abs(fontSize - bodyFont * factor) < 0.03);
    }
  }
  QVERIFY(p_state.value(QStringLiteral("listStructure")).toBool());
  QVERIFY(p_state.value(QStringLiteral("listsUnclipped")).toBool());
  const auto markers = p_state.value(QStringLiteral("listMarkers")).toArray();
  QCOMPARE(markers.size(), 2);
  const auto articleBounds = p_state.value(QStringLiteral("articles")).toObject();
  const double firstGutter = markers.first().toObject().value(QStringLiteral("gutter")).toDouble();
  QVERIFY(firstGutter >= bodyFont && firstGutter <= bodyFont * 2);
  for (int index = 0; index < markers.size(); ++index) {
    const auto marker = markers[index].toObject();
    QCOMPARE(marker.value(QStringLiteral("display")).toString(), QStringLiteral("list-item"));
    QCOMPARE(marker.value(QStringLiteral("type")).toString(), QStringLiteral("disc"));
    QCOMPARE(marker.value(QStringLiteral("position")).toString(), QStringLiteral("outside"));
    QVERIFY(marker.value(QStringLiteral("markerFont")).toDouble() >= bodyFont - 0.01);
    const auto color = marker.value(QStringLiteral("markerColor")).toString();
    QVERIFY(!color.isEmpty() && color != QStringLiteral("transparent") &&
            color != QStringLiteral("rgba(0, 0, 0, 0)"));
    QVERIFY(!marker.value(QStringLiteral("clipped")).toBool());
    QVERIFY(std::abs(marker.value(QStringLiteral("gutter")).toDouble() - firstGutter) < 1);
    const auto title = marker.value(QStringLiteral("title")).toObject();
    const auto item = marker.value(QStringLiteral("item")).toObject();
    QVERIFY(std::abs(title.value(QStringLiteral("top")).toDouble() -
                     item.value(QStringLiteral("top")).toDouble()) < 2);
    QVERIFY(title.value(QStringLiteral("width")).toDouble() >=
            articleBounds.value(QStringLiteral("width")).toDouble() - 4 * bodyFont);
    if (index > 0) {
      QVERIFY(marker.value(QStringLiteral("parentPadding")).toDouble() < 1);
    }
  }
  QVERIFY(p_state.value(QStringLiteral("articlesVisible")).toBool());
  for (const auto &visible : p_state.value(QStringLiteral("articleVisibility")).toArray()) {
    QVERIFY(visible.toBool());
  }
  QCOMPARE(p_state.value(QStringLiteral("noteText")).toString().simplified(),
           longNote().simplified());
  QCOMPARE(p_state.value(QStringLiteral("noteParagraphs")).toInt(), 1);
  QCOMPARE(p_state.value(QStringLiteral("noteMarkup")).toInt(), 0);
  const auto paragraphs = p_state.value(QStringLiteral("paragraphs")).toArray();
  QCOMPARE(paragraphs.size(), 2);
  QCOMPARE(paragraphs[0].toString(),
           QStringLiteral("First paragraph <b>is plain text</b>\nsecond line"));
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
  QCOMPARE(reference.value(QStringLiteral("title")).toString(),
           QStringLiteral("Across the forest"));
  QVERIFY(reference.value(QStringLiteral("ownRow")).toBool());
  QVERIFY(!reference.value(QStringLiteral("inTitle")).toBool());
  const auto tags = p_state.value(QStringLiteral("tags")).toArray();
  QCOMPARE(tags.size(), 3);
  for (int index = 0; index < tags.size(); ++index) {
    const auto tag = tags[index].toObject();
    QCOMPARE(tag.value(QStringLiteral("text")).toString(),
             index < 2 ? QStringLiteral("Research <&>") : QStringLiteral("Deep research"));
    QVERIFY(tag.value(QStringLiteral("visible")).toBool());
    QVERIFY(tag.value(QStringLiteral("rounded")).toBool());
  }
  const auto icons = p_state.value(QStringLiteral("icons")).toArray();
  QCOMPARE(icons.size(), 3);
  for (const auto &value : icons) {
    const auto icon = value.toObject();
    QVERIFY(!icon.value(QStringLiteral("text")).toString().isEmpty());
    QVERIFY(icon.value(QStringLiteral("visible")).toBool());
    QVERIFY(!icon.value(QStringLiteral("inHeading")).toBool());
    QVERIFY(icon.value(QStringLiteral("ownRow")).toBool());
  }
  const QStringList rowClasses{
      QStringLiteral("m3-article-title"),      QStringLiteral("m3-article-icons"),
      QStringLiteral("m3-article-tags"),       QStringLiteral("m3-article-url"),
      QStringLiteral("m3-article-references"), QStringLiteral("m3-article-note"),
      QStringLiteral("m3-article-figure")};
  const QStringList rowTags{QStringLiteral("P"),     QStringLiteral("DIV"), QStringLiteral("DIV"),
                            QStringLiteral("P"),     QStringLiteral("DIV"), QStringLiteral("DIV"),
                            QStringLiteral("FIGURE")};
  const auto rows = p_state.value(QStringLiteral("rows")).toArray();
  QCOMPARE(rows.size(), rowClasses.size());
  double previousBottom = rows.first()
                              .toObject()
                              .value(QStringLiteral("bounds"))
                              .toObject()
                              .value(QStringLiteral("top"))
                              .toDouble();
  for (int index = 0; index < rows.size(); ++index) {
    const auto row = rows[index].toObject();
    QCOMPARE(row.value(QStringLiteral("classes")).toString(), rowClasses[index]);
    QCOMPARE(row.value(QStringLiteral("tag")).toString(), rowTags[index]);
    QVERIFY(row.value(QStringLiteral("visible")).toBool());
    const auto bounds = row.value(QStringLiteral("bounds")).toObject();
    QVERIFY(bounds.value(QStringLiteral("top")).toDouble() >= previousBottom - 1);
    previousBottom = bounds.value(QStringLiteral("bottom")).toDouble();
  }
  QVERIFY(!p_state.value(QStringLiteral("emptyRows")).toBool());
  QCOMPARE(p_state.value(QStringLiteral("sparseRows")).toInt(), 1);
  const auto safeUrl = p_state.value(QStringLiteral("safeUrl")).toObject();
  QCOMPARE(safeUrl.value(QStringLiteral("tag")).toString(), QStringLiteral("P"));
  QCOMPARE(safeUrl.value(QStringLiteral("href")).toString(),
           QStringLiteral("https://example.invalid/deep?q=1&x=2"));
  QCOMPARE(safeUrl.value(QStringLiteral("text")).toString(),
           QStringLiteral("https://example.invalid/deep?q=1&x=2"));
  QCOMPARE(safeUrl.value(QStringLiteral("icon")).toString(), QString(QChar(0x2197)));
  QVERIFY(safeUrl.value(QStringLiteral("decorative")).toBool());
  QVERIFY(safeUrl.value(QStringLiteral("order")).toBool());
  const auto unsafeUrl = p_state.value(QStringLiteral("unsafeUrl")).toObject();
  QCOMPARE(unsafeUrl.value(QStringLiteral("links")).toInt(), 0);
  QCOMPARE(unsafeUrl.value(QStringLiteral("icons")).toInt(), 0);
  QCOMPARE(unsafeUrl.value(QStringLiteral("text")).toString(),
           QStringLiteral("javascript:alert('unsafe')"));
  const auto figures = p_state.value(QStringLiteral("figures")).toArray();
  QCOMPARE(figures.size(), 2);
  for (const auto &value : figures) {
    const auto figure = value.toObject();
    QVERIFY(figure.value(QStringLiteral("decoded")).toBool());
    QVERIFY(figure.value(QStringLiteral("visible")).toBool());
    QVERIFY(figure.value(QStringLiteral("fits")).toBool());
    QCOMPARE(figure.value(QStringLiteral("source")).toString(),
             p_state.value(QStringLiteral("source")).toString());
    const auto bounds = figure.value(QStringLiteral("bounds")).toObject();
    const double ratio = bounds.value(QStringLiteral("width")).toDouble() /
                         bounds.value(QStringLiteral("height")).toDouble();
    QVERIFY(std::abs(ratio - 123.0 / 67.0) < 0.01);
  }
}

void verifyMapDocument(const QJsonObject &p_state, bool p_tall = false) {
  QVERIFY(!p_state.value(QStringLiteral("inlineHandlers")).toBool());
  QVERIFY(p_state.value(QStringLiteral("directMap")).toBool());
  QVERIFY(p_state.value(QStringLiteral("ancestorsUnscaled")).toBool());
  QVERIFY(p_state.value(QStringLiteral("articlesUnscaled")).toBool());
  QCOMPARE(p_state.value(QStringLiteral("mapTransform")).toString(), QStringLiteral("none"));
  const auto nodeIds = p_state.value(QStringLiteral("nodeIds")).toArray();
  QCOMPARE(nodeIds.size(), topics(p_tall).size());
  for (int index = 0; index < topics(p_tall).size(); ++index) {
    QVERIFY(nodeIds.contains(QStringLiteral("n%1").arg(index)));
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
           "All SVG connectors and node bounds must remain inside the complete map");
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

void verifyNaturalDocument(const QJsonObject &p_state) {
  QVERIFY(p_state.value(QStringLiteral("mapVisible")).toBool());
  QVERIFY(p_state.value(QStringLiteral("articlesVisible")).toBool());
  QVERIFY(p_state.value(QStringLiteral("uncapped")).toBool());
  QVERIFY(p_state.value(QStringLiteral("naturalOverflow")).toBool());
  QCOMPARE(p_state.value(QStringLiteral("mapPosition")).toString(), QStringLiteral("relative"));
  const auto zoom = p_state.value(QStringLiteral("mapZoom")).toString();
  QVERIFY(zoom == QStringLiteral("normal") || std::abs(zoom.toDouble() - 1) < 0.001);
  const auto map = p_state.value(QStringLiteral("map")).toObject();
  const auto visual = p_state.value(QStringLiteral("visual")).toObject();
  const auto articles = p_state.value(QStringLiteral("articles")).toObject();
  const auto main = p_state.value(QStringLiteral("main")).toObject();
  const double width = map.value(QStringLiteral("width")).toDouble();
  const double height = map.value(QStringLiteral("height")).toDouble();
  QVERIFY(width > 0 && height > 0);
  QVERIFY(std::abs(width - p_state.value(QStringLiteral("naturalWidth")).toDouble()) < 1);
  QVERIFY(std::abs(height - p_state.value(QStringLiteral("naturalHeight")).toDouble()) < 1);
  QVERIFY(visual.value(QStringLiteral("height")).toDouble() >= height - 1);
  QVERIFY(articles.value(QStringLiteral("top")).toDouble() >=
          visual.value(QStringLiteral("bottom")).toDouble());
  QVERIFY(p_state.value(QStringLiteral("documentWidth")).toDouble() >=
          map.value(QStringLiteral("right")).toDouble() +
              p_state.value(QStringLiteral("scrollX")).toDouble() - 1);
  QVERIFY(p_state.value(QStringLiteral("documentHeight")).toDouble() >=
          articles.value(QStringLiteral("bottom")).toDouble() +
              p_state.value(QStringLiteral("scrollY")).toDouble() - 1);
  QVERIFY(std::abs(articles.value(QStringLiteral("width")).toDouble() -
                   main.value(QStringLiteral("width")).toDouble()) < 1);
  QVERIFY(p_state.value(QStringLiteral("viewportWidth")).toDouble() -
              main.value(QStringLiteral("width")).toDouble() <=
          4 * p_state.value(QStringLiteral("bodyFont")).toDouble());
  QVERIFY(!p_state.value(QStringLiteral("chromeHidden")).toBool());
  QVERIFY(p_state.value(QStringLiteral("separatorVisible")).toBool());
  QCOMPARE(p_state.value(QStringLiteral("breakAfter")).toString(), QStringLiteral("auto"));
  QCOMPARE(p_state.value(QStringLiteral("pageBreakAfter")).toString(), QStringLiteral("auto"));
}

void verifyPdfMapReservation(const QJsonObject &p_state) {
  QCOMPARE(p_state.value(QStringLiteral("mapPosition")).toString(), QStringLiteral("absolute"));
  QCOMPARE(p_state.value(QStringLiteral("visualPosition")).toString(), QStringLiteral("relative"));
  QCOMPARE(p_state.value(QStringLiteral("visualOverflow")).toString(), QStringLiteral("hidden"));
  const auto map = p_state.value(QStringLiteral("map")).toObject();
  const auto visual = p_state.value(QStringLiteral("visual")).toObject();
  const double scale = map.value(QStringLiteral("width")).toDouble() /
                       p_state.value(QStringLiteral("naturalWidth")).toDouble();
  QVERIFY(scale > 0 && scale <= 1.001);
  const auto zoom = p_state.value(QStringLiteral("mapZoom")).toString();
  QVERIFY(std::abs((zoom == QStringLiteral("normal") ? 1 : zoom.toDouble()) - scale) < 0.001);
  for (const auto &edge : {QStringLiteral("left"), QStringLiteral("top"), QStringLiteral("width"),
                           QStringLiteral("height")}) {
    QVERIFY(std::abs(map.value(edge).toDouble() - visual.value(edge).toDouble()) < 1);
  }
  QCOMPARE(p_state.value(QStringLiteral("bodyFont")).toDouble(), 16.0);
  QCOMPARE(p_state.value(QStringLiteral("noteFont")).toDouble(), 16.0);
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
    pages.push({items:content.items.map(item => ({text:item.str, height:item.height,
                                                hasEOL:item.hasEOL})),
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
  void naturalSizeDocumentAndReadableArticles_data();
  void naturalSizeDocumentAndReadableArticles();
  void printableNativeForest_data();
  void printableNativeForest();
  void largeHtmlAndLocalSidecarProducePdf();
  void invalidInputsLeaveNoOutput();
  void delayedImageSettlesBeforePdf();
  void cancellationDiscardsLateResponseAndConverterCanBeReused();
};

void TestHtmlPdfExporter::naturalSizeDocumentAndReadableArticles_data() {
  QTest::addColumn<bool>("tall");
  QTest::addColumn<bool>("custom");
  QTest::newRow("standalone-wide") << false << false;
  QTest::newRow("standalone-tall") << true << false;
  QTest::newRow("custom-wide") << false << true;
  QTest::newRow("custom-tall") << true << true;
}

void TestHtmlPdfExporter::naturalSizeDocumentAndReadableArticles() {
  QFETCH(bool, tall);
  QFETCH(bool, custom);
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  QVERIFY(writeFile(directory.filePath(QStringLiteral("fixture.png")), imageBytes()));
  ServiceLocator services;
  MindMapExporter renderer(services, nullptr, nullptr);
  ExportHtmlRequest request;
  request.purpose = custom ? ExportHtmlPurpose::Custom : ExportHtmlPurpose::Standalone;
  // Neither complete HTML presentation depends on printable paper geometry.
  request.options.m_pdfOption.m_layout.clear();
  const auto html = directory.filePath(QStringLiteral("natural.html"));
  QVERIFY(renderer.renderHtml(nativeInput(directory.path(), tall), request, html));

  BrowserPage browser;
  QVERIFY(browser.profile.isOffTheRecord());
  QVERIFY(browser.page.settings()->testAttribute(QWebEngineSettings::JavascriptEnabled));
  QVERIFY(loadFile(browser.page, html));
  auto state = documentState(browser.page);
  QVERIFY(!state.isEmpty());
  verifyMapDocument(state, tall);
  verifyCompleteArticles(state, tall);
  verifyNaturalDocument(state);
  const double width = state.value(QStringLiteral("naturalWidth")).toDouble();
  const double height = state.value(QStringLiteral("naturalHeight")).toDouble();
  QVERIFY(width > state.value(QStringLiteral("viewportWidth")).toDouble() + 100);
  if (tall) {
    QVERIFY(height > state.value(QStringLiteral("viewportHeight")).toDouble() + 100);
  }

  double smallArticleWidth = 0;
  for (const auto &size : {QSize(640, 700), QSize(1800, 1000)}) {
    browser.view.resize(size);
    QTRY_COMPARE_WITH_TIMEOUT(
        evaluateJavaScript(browser.page, QStringLiteral("innerWidth")).toInt(), size.width(), 5000);
    QVERIFY(settleLayout(browser.page));
    state = documentState(browser.page);
    QVERIFY(!state.isEmpty());
    verifyMapDocument(state, tall);
    verifyCompleteArticles(state, tall);
    verifyNaturalDocument(state);
    const auto map = state.value(QStringLiteral("map")).toObject();
    QVERIFY(std::abs(map.value(QStringLiteral("width")).toDouble() - width) < 1);
    QVERIFY(std::abs(map.value(QStringLiteral("height")).toDouble() - height) < 1);
    const double articleWidth = state.value(QStringLiteral("articles"))
                                    .toObject()
                                    .value(QStringLiteral("width"))
                                    .toDouble();
    if (size.width() == 640) {
      smallArticleWidth = articleWidth;
    } else {
      QVERIFY(articleWidth > smallArticleWidth + 1000);
      QVERIFY(articleWidth > state.value(QStringLiteral("oldColumnWidth")).toDouble() + 500);
    }
  }

  // The document itself exposes the complete map and the article tail, not an inner scroller.
  browser.view.resize(640, 700);
  QTRY_COMPARE_WITH_TIMEOUT(evaluateJavaScript(browser.page, QStringLiteral("innerWidth")).toInt(),
                            640, 5000);
  QVERIFY(evaluateJavaScript(browser.page, QStringLiteral(R"JS((() => {
    window.scrollTo(document.scrollingElement.scrollWidth, document.scrollingElement.scrollHeight);
    return true;
  })())JS"))
              .toBool());
  QVERIFY(settleLayout(browser.page));
  state = documentState(browser.page);
  QVERIFY(state.value(QStringLiteral("scrollX")).toDouble() > 100);
  QVERIFY(state.value(QStringLiteral("scrollY")).toDouble() > height);
  verifyNaturalDocument(state);

  QVERIFY(evaluateJavaScript(browser.page, QStringLiteral(R"JS((() => {
    const topic = document.querySelector('#mindmap .m3-topic');
    topic.scrollIntoView({block:'center', inline:'center'});
    const box = topic.getBoundingClientRect();
    const hit = document.elementFromPoint(box.left + 4, box.top + 7);
    return hit && hit.closest('.m3-topic') === topic;
  })())JS"))
              .toBool());
  const auto selected = evaluateJavaScript(browser.page, QStringLiteral(R"JS((() => {
    const range = document.createRange();
    range.selectNodeContents(document.querySelector('#mindmap .m3-topic'));
    const selection = window.getSelection();
    selection.removeAllRanges(); selection.addRange(range);
    const text = selection.toString(); selection.removeAllRanges();
    return text;
  })())JS"))
                            .toString();
  QCOMPARE(selected.simplified(), topics().first().simplified());

  // Real pointer input follows the map topic, then the reference on its separate article row.
  QVERIFY(clickElement(browser, QStringLiteral("#mindmap [data-node-id='n7'] .m3-topic")));
  QTRY_COMPARE_WITH_TIMEOUT(
      evaluateJavaScript(browser.page, QStringLiteral("location.hash")).toString(),
      QStringLiteral("#m3-node-6e37"), 5000);
  QVERIFY(evaluateJavaScript(browser.page, QStringLiteral(R"JS((() => {
    const title = document.querySelector('#m3-node-6e37 .m3-article-title').getBoundingClientRect();
    return title.top >= 0 && title.bottom <= innerHeight;
  })())JS"))
              .toBool());
  QVERIFY(clickElement(browser, QStringLiteral("#m3-node-6e37 .m3-cross-reference")));
  QTRY_COMPARE_WITH_TIMEOUT(
      evaluateJavaScript(browser.page, QStringLiteral("location.hash")).toString(),
      QStringLiteral("#m3-node-6e39"), 5000);
  QVERIFY(evaluateJavaScript(browser.page, QStringLiteral(R"JS((() => {
    const title = document.querySelector('#m3-node-6e39 .m3-article-title').getBoundingClientRect();
    return title.top >= 0 && title.bottom <= innerHeight;
  })())JS"))
              .toBool());
  state = documentState(browser.page);
  verifyNaturalDocument(state);
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
  verifyPdfMapReservation(state);
  QVERIFY(!state.value(QStringLiteral("separatorVisible")).toBool());
  QVERIFY(state.value(QStringLiteral("chromeHidden")).toBool());
  const auto map = state.value(QStringLiteral("map")).toObject();
  const auto visual = state.value(QStringLiteral("visual")).toObject();
  const auto articles = state.value(QStringLiteral("articles")).toObject();
  const double width = map.value(QStringLiteral("width")).toDouble();
  const double height = map.value(QStringLiteral("height")).toDouble();
  QVERIFY(width > 0 && height > 0);
  QVERIFY(width <= (printable.width() - 1) * pixelsPerMillimeter + 1);
  QVERIFY(height <= (printable.height() - 1) * pixelsPerMillimeter + 1);
  QVERIFY(std::abs(map.value(QStringLiteral("left")).toDouble()) < 1);
  QVERIFY(std::abs(map.value(QStringLiteral("top")).toDouble()) < 1);
  QVERIFY(visual.value(QStringLiteral("bottom")).toDouble() <=
          printable.height() * pixelsPerMillimeter);
  const double nativeRatio = state.value(QStringLiteral("naturalWidth")).toDouble() /
                             state.value(QStringLiteral("naturalHeight")).toDouble();
  QVERIFY(std::abs(width / height - nativeRatio) / nativeRatio < 0.005);
  for (const auto &value : state.value(QStringLiteral("figures")).toArray()) {
    const auto figure = value.toObject().value(QStringLiteral("bounds")).toObject();
    QVERIFY(figure.value(QStringLiteral("width")).toDouble() <=
            printable.width() * pixelsPerMillimeter);
    QVERIFY(figure.value(QStringLiteral("height")).toDouble() <=
            printable.height() * pixelsPerMillimeter);
  }
  QVERIFY(articles.value(QStringLiteral("top")).toDouble() >=
          visual.value(QStringLiteral("bottom")).toDouble());
  QVERIFY(state.value(QStringLiteral("breakAfter")).toString() == QStringLiteral("page") ||
          state.value(QStringLiteral("pageBreakAfter")).toString() == QStringLiteral("always"));
  QVERIFY(
      state.value(QStringLiteral("note")).toObject().value(QStringLiteral("height")).toDouble() >
      printable.height() * pixelsPerMillimeter);
  QCOMPARE(state.value(QStringLiteral("noteBreak")).toString(), QStringLiteral("auto"));
  QVERIFY(
      state.value(QStringLiteral("headingBreak")).toString().startsWith(QStringLiteral("avoid")));

  // Resizing cannot override paper geometry or introduce another layout zoom.
  browser.view.resize(400, 700);
  QTRY_COMPARE_WITH_TIMEOUT(evaluateJavaScript(browser.page, QStringLiteral("innerWidth")).toInt(),
                            400, 5000);
  QVERIFY(settleLayout(browser.page));
  const auto resized = documentState(browser.page);
  verifyMapDocument(resized);
  const auto resizedMap = resized.value(QStringLiteral("map")).toObject();
  QVERIFY(std::abs(resizedMap.value(QStringLiteral("width")).toDouble() - width) < 1);
  QVERIFY(std::abs(resizedMap.value(QStringLiteral("height")).toDouble() - height) < 1);
  verifyPdfMapReservation(resized);

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
  constexpr double pointsPerCssPixel = 72.0 / 96.0;
  constexpr double imageExtentTolerance = pointsPerCssPixel + 1e-4;
  const auto mapZoom = state.value(QStringLiteral("mapZoom")).toString();
  const double mapScale = mapZoom == QStringLiteral("normal") ? 1.0 : mapZoom.toDouble();
  for (int index = 0; index < pages.size(); ++index) {
    const auto page = pages[index].toObject();
    QCOMPARE(page.value(QStringLiteral("width")).toDouble() >
                 page.value(QStringLiteral("height")).toDouble(),
             landscape);
    QString text;
    for (const auto &item : page.value(QStringLiteral("items")).toArray()) {
      const auto entry = item.toObject();
      const auto line = entry.value(QStringLiteral("text")).toString();
      // PDF.js may split within a word (for example, at a ligature).
      text += line;
      if (entry.value(QStringLiteral("hasEOL")).toBool()) {
        text += QLatin1Char('\n');
      }
      if (index > 0 && line.contains(QStringLiteral("Paragraph "))) {
        const double height = entry.value(QStringLiteral("height")).toDouble();
        QVERIFY2(std::abs(height - 12.0) < 0.1,
                 qPrintable(QStringLiteral("Map overflow/zoom shrank body text to %1 pt on page %2")
                                .arg(height)
                                .arg(index + 1)));
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
    const double imageScale = index == 0 ? mapScale : 1.0;
    const double expectedImageWidth = 123.0 * imageScale * pointsPerCssPixel;
    const double expectedImageHeight = 67.0 * imageScale * pointsPerCssPixel;
    for (const auto &value : images) {
      const auto image = value.toObject();
      const double imageWidth = image.value(QStringLiteral("width")).toDouble();
      const double imageHeight = image.value(QStringLiteral("height")).toDouble();
      QVERIFY(imageWidth > 0 && imageHeight > 0);
      // Chromium snaps image rectangle edges to CSS pixels before printing, so each extent
      // may differ by one CSS pixel. Check both axes to catch distortion and unintended scaling.
      QVERIFY2(std::abs(imageWidth - expectedImageWidth) <= imageExtentTolerance &&
                   std::abs(imageHeight - expectedImageHeight) <= imageExtentTolerance,
               qPrintable(QStringLiteral("Image %1 x %2 pt on page %3 (%4 x %5 pt): "
                                         "expected %6 x %7 pt +/- %8 pt per axis; map zoom %9")
                              .arg(imageWidth, 0, 'g', 15)
                              .arg(imageHeight, 0, 'g', 15)
                              .arg(index + 1)
                              .arg(page.value(QStringLiteral("width")).toDouble(), 0, 'g', 15)
                              .arg(page.value(QStringLiteral("height")).toDouble(), 0, 'g', 15)
                              .arg(expectedImageWidth, 0, 'g', 15)
                              .arg(expectedImageHeight, 0, 'g', 15)
                              .arg(imageExtentTolerance, 0, 'g', 15)
                              .arg(mapZoom)));
      QVERIFY(imageWidth <= printable.width() * 72.0 / 25.4 + 1);
      QVERIFY(imageHeight <= printable.height() * 72.0 / 25.4 + 1);
    }
  }
  const auto firstPage = pageText.first();
  QVERIFY(firstPage.contains(QStringLiteral("Root")));
  for (const auto &level :
       {QStringLiteral("two"), QStringLiteral("three"), QStringLiteral("four"),
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
