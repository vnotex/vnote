#include "mainconfig.h"

#include <QDebug>
#include <QJsonObject>
#include <QVersionNumber>

#include "coreconfig.h"
#include "editorconfig.h"
#include "iconfigmgr.h"
#include "markdowneditorconfig.h"
#include "pdfviewerconfig.h"
#include "texteditorconfig.h"
#include "widgetconfig.h"

using namespace vnotex;

MainConfig::MainConfig(IConfigMgr *p_mgr) : IConfig(p_mgr, nullptr) {
  m_childConfigs.resize(ChildConfigIndex::ChildConfigCount);
  m_childConfigs[ChildConfigIndex::CoreConfigIndex].reset(new CoreConfig(p_mgr, this));
  m_childConfigs[ChildConfigIndex::EditorConfigIndex].reset(new EditorConfig(p_mgr, this));
  m_childConfigs[ChildConfigIndex::WidgetConfigIndex].reset(new WidgetConfig(p_mgr, this));
}

MainConfig::~MainConfig() {}

void MainConfig::fromJson(const QJsonObject &p_jobj) {
  // p_jobj MUST already be merged (defaults + user overrides). ConfigMgr2::init() guarantees
  // this by applying the user's document as an RFC 7386 merge patch on top of the defaults;
  // the child fromJson()s below map an absent key to false/0/"", so a raw, unmerged document
  // would wipe every non-zero default.
  loadMetadata(p_jobj);

  for (auto &childConfig : m_childConfigs) {
    Q_ASSERT(childConfig);
    childConfig->fromJson(p_jobj.value(childConfig->getSectionName()).toObject());
  }
}

void MainConfig::loadMetadata(const QJsonObject &p_jobj) {
  // Extract metadata from merged JSON
  const auto metaObj = p_jobj.value(QStringLiteral("metadata")).toObject();

  m_version = metaObj.value(QStringLiteral("version")).toString();
}

QJsonObject MainConfig::saveMetaData() const {
  QJsonObject metaObj;
  metaObj[QStringLiteral("version")] = m_version;
  return metaObj;
}

CoreConfig &MainConfig::getCoreConfig() {
  return *static_cast<CoreConfig *>(m_childConfigs[ChildConfigIndex::CoreConfigIndex].data());
}

EditorConfig &MainConfig::getEditorConfig() {
  return *static_cast<EditorConfig *>(m_childConfigs[ChildConfigIndex::EditorConfigIndex].data());
}

WidgetConfig &MainConfig::getWidgetConfig() {
  return *static_cast<WidgetConfig *>(m_childConfigs[ChildConfigIndex::WidgetConfigIndex].data());
}

void MainConfig::update() { getMgr()->updateMainConfig(toJson()); }

QJsonObject MainConfig::toJson() const {
  QJsonObject obj;
  obj[QStringLiteral("metadata")] = saveMetaData();
  for (const auto &childConfig : m_childConfigs) {
    Q_ASSERT(childConfig);
    obj[childConfig->getSectionName()] = childConfig->toJson();
  }
  return obj;
}

void MainConfig::doVersionSpecificOverride(const QString &p_previousVersion) {
  // In a new version, we may want to change one value by force so that a stale
  // user override no longer masks a new default. Each override MUST be gated on
  // the version it was introduced in, so it runs only once for the relevant
  // upgrade and never destroys config on downgrade or later upgrades.

  // 4.4.4: the interactive table in-place preview became a default source. An
  // existing installation has "inplacePreviewSources" already persisted without
  // it (fromJson rebuilds the flags purely from that string), so the new C++
  // default would only ever reach a fresh start. Add the flag once, without
  // touching the other sources the user may have turned off.
  //
  // A user who turned OFF every source is left alone: that empty set is a
  // blanket opt-out from in-place preview, and this preview can rewrite the
  // Markdown source, so it must not become their only enabled source.
  //
  // Known limitation: a 4.4.4+ -> 4.4.3 downgrade stamps the version back down
  // (older builds have no notion of a newer config), so a later re-upgrade runs
  // this override a second time and re-enables Table for a user who had turned
  // it off. Undoing that would need a migration marker outside the config JSON,
  // which is not worth it for a single checkbox.
  // Gated on the previous version only, not on ConfigMgr2::c_version: the flag
  // exists from this build onward, so a config written by any older build should
  // pick it up at the first version change, whichever release that turns out to
  // be.
  static const QVersionNumber c_tableSourceVersion(4, 4, 4);
  if (QVersionNumber::fromString(p_previousVersion) < c_tableSourceVersion) {
    auto &mdConfig = getEditorConfig().getMarkdownEditorConfig();
    const auto srcs = mdConfig.getInplacePreviewSources();
    if (srcs != MarkdownEditorConfig::InplacePreviewSources(
                    MarkdownEditorConfig::InplacePreviewSource::NoInplacePreview)) {
      mdConfig.setInplacePreviewSources(srcs | MarkdownEditorConfig::InplacePreviewSource::Table);
    }
  }

  // 4.6.0: pdf.js was upgraded from v3.11.174 to v6.2.108. The bundle is ESM-only,
  // so `build/pdf.js` / `web/viewer.js` / `pdfviewer.js` no longer exist and are
  // replaced by `build/pdf.mjs` / `web/viewer.mjs` / `pdfviewer.mjs`.
  //
  // This override is MANDATORY, not cosmetic. `editor.pdf_viewer.viewerResource` is
  // persisted verbatim for every existing user, and WebResource::init() takes the
  // persisted object WHOLESALE (it even resizes the resource vector from the JSON),
  // so there is no app-vs-user merge that could heal it. Without the reset, every
  // upgrading user's PDF viewer would silently load three files that are not on
  // disk and render a blank page.
  //
  // Same known limitation as the 4.4.4 override above: a downgrade stamps the
  // version back down, so a later re-upgrade re-runs this and discards any manual
  // customization of the script list a second time. That is the correct trade —
  // a stale list here is a non-functional viewer, not a checkbox.
  static const QVersionNumber c_pdfJsV6Version(4, 6, 0);
  if (QVersionNumber::fromString(p_previousVersion) < c_pdfJsV6Version) {
    auto &pdfConfig = getEditorConfig().getPdfViewerConfig();
    pdfConfig.m_viewerResource = PdfViewerConfig::defaultViewerResource();
  }
}

QString MainConfig::peekVersion(const QJsonObject &p_jboj) {
  const auto metaObj = p_jboj.value(QStringLiteral("metadata")).toObject();
  return metaObj.value(QStringLiteral("version")).toString();
}
