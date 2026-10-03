#include "mindmapeditorconfig.h"

using namespace vnotex;

MindMapEditorConfig::MindMapEditorConfig(IConfigMgr *p_mgr, IConfig *p_topConfig)
    : IConfig(p_mgr, p_topConfig) {
  m_sectionName = QStringLiteral("mindMapEditor");
}

void MindMapEditorConfig::fromJson(const QJsonObject &p_jobj) {
  const auto style = p_jobj.value(QStringLiteral("style")).toObject();
  m_fontFamily = style.value(QStringLiteral("font-family")).toString().trimmed();
  m_fontPointSize = qBound(0, style.value(QStringLiteral("font-size")).toInt(0), 256);
  m_undoLimit = qMax(0, p_jobj.value(QStringLiteral("undoLimit")).toInt(100));
  m_confirmSubtreeDeletion = p_jobj.value(QStringLiteral("confirmSubtreeDeletion")).toBool(true);
  m_autoRandomBranchColor = p_jobj.value(QStringLiteral("autoRandomBranchColor")).toBool(true);
  m_shortcuts = p_jobj.value(QStringLiteral("shortcuts")).toObject();
  m_effectiveShortcuts = m_shortcuts;
}

QJsonObject MindMapEditorConfig::toJson() const {
  QJsonObject style;
  style[QStringLiteral("font-family")] = m_fontFamily;
  style[QStringLiteral("font-size")] = m_fontPointSize;

  QJsonObject obj;
  obj[QStringLiteral("style")] = style;
  obj[QStringLiteral("undoLimit")] = m_undoLimit;
  obj[QStringLiteral("confirmSubtreeDeletion")] = m_confirmSubtreeDeletion;
  obj[QStringLiteral("autoRandomBranchColor")] = m_autoRandomBranchColor;
  obj[QStringLiteral("shortcuts")] = m_shortcuts;
  return obj;
}

const QString &MindMapEditorConfig::getFontFamily() const { return m_fontFamily; }

void MindMapEditorConfig::setFontFamily(const QString &p_family) {
  updateConfig(m_fontFamily, p_family.trimmed(), this);
}

int MindMapEditorConfig::getFontPointSize() const { return m_fontPointSize; }

void MindMapEditorConfig::setFontPointSize(int p_size) {
  updateConfig(m_fontPointSize, qBound(0, p_size, 256), this);
}

int MindMapEditorConfig::getUndoLimit() const { return m_undoLimit; }

void MindMapEditorConfig::setUndoLimit(int p_limit) {
  updateConfig(m_undoLimit, qMax(0, p_limit), this);
}

bool MindMapEditorConfig::getConfirmSubtreeDeletion() const { return m_confirmSubtreeDeletion; }

void MindMapEditorConfig::setConfirmSubtreeDeletion(bool p_enabled) {
  updateConfig(m_confirmSubtreeDeletion, p_enabled, this);
}

bool MindMapEditorConfig::getAutoRandomBranchColor() const { return m_autoRandomBranchColor; }

void MindMapEditorConfig::setAutoRandomBranchColor(bool p_enabled) {
  updateConfig(m_autoRandomBranchColor, p_enabled, this);
}

const QJsonObject &MindMapEditorConfig::getShortcuts() const { return m_effectiveShortcuts; }
