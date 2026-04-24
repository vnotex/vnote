// Stubs for symbols referenced by textviewwindowcontroller.cpp but not
// needed by the static methods under test.  Linking the real ConfigMgr2 /
// CoreConfig would pull in deep cascading dependencies that are not
// available in the test harness.

#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/editorconfig.h>
#include <core/mainconfig.h>
#include <core/sessionconfig.h>
#include <gui/utils/widgetutils.h>

#include <QAction>
#include <QKeySequence>

using namespace vnotex;

// --- ConfigMgr2 stubs ---

ConfigMgr2::~ConfigMgr2() = default;

void ConfigMgr2::updateMainConfig(const QJsonObject &p_jobj) {
  Q_UNUSED(p_jobj);
}

void ConfigMgr2::updateSessionConfig(const QJsonObject &p_jobj) {
  Q_UNUSED(p_jobj);
}

void ConfigMgr2::doWriteMainConfig() {}

void ConfigMgr2::doWriteSessionConfig() {}


CoreConfig &ConfigMgr2::getCoreConfig() {
  static CoreConfig *s = nullptr;
  Q_ASSERT_X(false, "stub", "ConfigMgr2::getCoreConfig() called in test - not expected");
  return *s; // unreachable
}

EditorConfig &ConfigMgr2::getEditorConfig() {
  static EditorConfig *s = nullptr;
  Q_ASSERT_X(false, "stub", "ConfigMgr2::getEditorConfig() called in test - not expected");
  return *s; // unreachable
}

// --- CoreConfig stub ---

const QString &CoreConfig::getShortcutLeaderKey() const {
  Q_ASSERT_X(false, "stub", "CoreConfig::getShortcutLeaderKey() called in test - not expected");
  static QString s;
  return s; // unreachable
}

// --- WidgetUtils stubs ---

void WidgetUtils::addActionShortcutText(QAction *p_action, const QString &p_shortcut) {
  if (p_shortcut.isEmpty()) {
    return;
  }

  QKeySequence kseq(p_shortcut);
  if (kseq.isEmpty()) {
    return;
  }

  p_action->setText(
      QStringLiteral("%1\t%2").arg(p_action->text(), kseq.toString(QKeySequence::NativeText)));
}
