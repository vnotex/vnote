#ifndef PROPERTYDEFS_H
#define PROPERTYDEFS_H

namespace vnotex {
// Define properties used for QSS.
class PropertyDefs {
public:
  PropertyDefs() = delete;

  static const char *c_actionToolButton;

  static const char *c_toolButtonWithoutMenuIndicator;

  static const char *c_dangerButton;

  static const char *c_dialogCentralWidget;

  static const char *c_viewSplitCornerWidget;

  static const char *c_viewSplitFlash;

  static const char *c_viewWindowToolBar;

  static const char *c_consoleTextEdit;

  static const char *c_embeddedLineEdit;

  // Values: info/warning/error.
  static const char *c_state;

  // InlineBanner severity. Values: info/warning/error. Separate from c_state
  // so the universal `*[State="..."]` border rule cannot fight the banner's
  // own styling.
  static const char *c_bannerSeverity;

  // Semantic TEXT color for any widget. Values: info/warning/error; unset or
  // empty means the default color. Use this instead of a hardcoded hex in a
  // setStyleSheet() call.
  static const char *c_severityText;

  // Muted / secondary text (hints, captions). Value: true. Do NOT use
  // setEnabled(false) for this — it lies to accessibility tooling, and the
  // themes style QLabel unconditionally so it would not even mute the color.
  static const char *c_mutedText;

  static const char *c_dockWidgetIndex;

  static const char *c_dockWidgetTitle;

  static const char *c_hitSettingWidget;

  static const char *c_mainWindowSideBar;

  static const char *c_settingsCard;

  static const char *c_settingsCardTitle;

  static const char *c_settingsDescription;

  static const char *c_settingsRow;

  static const char *c_settingsSeparator;
};
} // namespace vnotex

#endif // PROPERTYDEFS_H
