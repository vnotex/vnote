#ifndef TEXTVIEWWINDOWCONTROLLER_H
#define TEXTVIEWWINDOWCONTROLLER_H

#include <QObject>
#include <QSharedPointer>
#include <QString>

namespace vte {
class TextEditorConfig;
struct TextEditorParameters;
} // namespace vte

namespace vnotex {

class ServiceLocator;
class EditorConfig;
class TextEditorConfig;
class Buffer2;

class TextViewWindowController : public QObject {
  Q_OBJECT
public:
  explicit TextViewWindowController(ServiceLocator &p_services, QObject *p_parent = nullptr);

  // State snapshot for applying buffer content to the editor.
  struct BufferState {
    QString content;
    QString syntaxSuffix; // file extension for syntax highlighting
    bool readOnly = false;
    bool modified = false;
    bool valid = false;
    int revision = 0;
  };

  // State snapshot for applying config values to the editor.
  struct EditorConfigSnapshot {
    int zoomDelta = 0;
    QString shortcutLeaderKey; // raw key string from CoreConfig
  };

  // ============ Config Revision Tracking ============

  // Check if editor config has changed. Updates internal revision tracking.
  // Returns true if config changed since last check.
  bool checkAndUpdateConfigRevision();

  // ============ Config Building ============

  // Build vte::TextEditorConfig from VNote config + theme data.
  // @p_themeFile: path to text editor style file (from ThemeService).
  // @p_syntaxTheme: editor highlight theme name (from ThemeService).
  // @p_scaleFactor: screen scale factor (from WidgetUtils).
  static QSharedPointer<vte::TextEditorConfig>
  buildTextEditorConfig(const EditorConfig &p_editorConfig, const TextEditorConfig &p_config,
                        const QString &p_themeFile, const QString &p_syntaxTheme,
                        qreal p_scaleFactor);

  // Build vte::TextEditorParameters from VNote config.
  static QSharedPointer<vte::TextEditorParameters>
  buildTextEditorParameters(const EditorConfig &p_editorConfig, const TextEditorConfig &p_config);

  // Get current editor config values for view application.
  EditorConfigSnapshot currentEditorConfig() const;

  // ============ Buffer State ============

  // Prepare buffer state for the view to apply to the editor.
  static BufferState prepareBufferState(const Buffer2 &p_buffer);

  // ============ Zoom ============

  // Persist zoom delta to config. Returns the persisted delta.
  int persistZoomDelta(int p_delta);

private:
  ServiceLocator &m_services;
  int m_editorConfigRevision = 0;
  int m_textEditorConfigRevision = 0;
};

} // namespace vnotex

#endif // TEXTVIEWWINDOWCONTROLLER_H
