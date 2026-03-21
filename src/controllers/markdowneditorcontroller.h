#ifndef MARKDOWNEDITORCONTROLLER_H
#define MARKDOWNEDITORCONTROLLER_H

#include <QObject>
#include <QSharedPointer>
#include <QString>

#include <core/markdowneditorconfig.h>

namespace vte {
class MarkdownEditorConfig;
struct TextEditorParameters;
} // namespace vte

namespace vnotex {

class ServiceLocator;
class EditorConfig;
class TextEditorConfig;
class Buffer2;

class MarkdownEditorController : public QObject {
  Q_OBJECT
public:
  explicit MarkdownEditorController(ServiceLocator &p_services, QObject *p_parent = nullptr);

  // State snapshot for applying buffer content to the editor.
  struct BufferState {
    QString content;
    QString basePath;     // directory containing the file (for image resolution)
    bool readOnly = false;
    bool modified = false;
    bool valid = false;
    int revision = 0;
  };

  // State snapshot for applying config values to the editor.
  struct EditorConfigSnapshot {
    int zoomDelta = 0;
    QString shortcutLeaderKey;     // raw key string from CoreConfig
    bool sectionNumberEnabled = false;
  };

  // Preview helper configuration extracted from MarkdownEditorConfig.
  struct PreviewHelperConfig {
    bool webPlantUmlEnabled = false;
    bool webGraphvizEnabled = false;
    bool inplacePreviewCodeBlocksEnabled = false;
    bool inplacePreviewMathBlocksEnabled = false;
  };

  // ============ Config Revision Tracking ============

  // Check if any editor-related config has changed (EditorConfig, TextEditorConfig,
  // MarkdownEditorConfig). Updates internal revision tracking.
  // Returns true if any config changed since last check.
  bool checkAndUpdateConfigRevision();

  // ============ Config Building ============

  // Build vte::MarkdownEditorConfig from VNote config + theme data.
  // @p_editorConfig: top-level EditorConfig (for ViConfig, line ending policy).
  // @p_mdConfig: MarkdownEditorConfig (for markdown-specific settings).
  // @p_themeFile: path to markdown editor style file (from ThemeService).
  // @p_syntaxTheme: editor highlight theme name (from ThemeService).
  // @p_scaleFactor: screen scale factor (from WidgetUtils).
  static QSharedPointer<vte::MarkdownEditorConfig>
  buildMarkdownEditorConfig(const EditorConfig &p_editorConfig,
                            const MarkdownEditorConfig &p_mdConfig,
                            const QString &p_themeFile,
                            const QString &p_syntaxTheme,
                            qreal p_scaleFactor);

  // Build vte::TextEditorParameters from VNote config (spell check settings).
  // @p_editorConfig: top-level EditorConfig (for auto-detect language, default dictionary).
  // @p_mdConfig: MarkdownEditorConfig (for markdown-specific spell check override).
  static QSharedPointer<vte::TextEditorParameters>
  buildMarkdownEditorParameters(const EditorConfig &p_editorConfig,
                                const MarkdownEditorConfig &p_mdConfig);

  // Get current editor config values for view application.
  EditorConfigSnapshot currentEditorConfig() const;

  // ============ Buffer State ============

  // Prepare buffer state for the view to apply to the editor.
  // Extracts content, basePath, modified flag, revision from Buffer2.
  static BufferState prepareBufferState(const Buffer2 &p_buffer);

  // ============ Section Number ============

  // Determine whether section numbering should be enabled for current mode.
  // @p_sectionNumberMode: from MarkdownEditorConfig::getSectionNumberMode().
  // @p_mode: current ViewWindowMode (Edit or Read).
  static bool shouldEnableSectionNumber(MarkdownEditorConfig::SectionNumberMode p_sectionNumberMode,
                                        int p_mode);

  // ============ Zoom Persistence ============

  // Persist text editor zoom delta to config. Returns the persisted delta.
  int persistZoomDelta(int p_delta);

  // Persist viewer zoom factor to config. Returns the persisted factor.
  qreal persistViewerZoomFactor(qreal p_factor);

  // ============ Preview Config ============

  // Extract preview helper configuration from MarkdownEditorConfig.
  // Used by MarkdownViewWindow2 to configure PreviewHelper.
  static PreviewHelperConfig getPreviewHelperConfig(const MarkdownEditorConfig &p_mdConfig);

private:
  ServiceLocator &m_services;
  int m_editorConfigRevision = 0;
  int m_textEditorConfigRevision = 0;
  int m_markdownEditorConfigRevision = 0;
};

} // namespace vnotex

#endif // MARKDOWNEDITORCONTROLLER_H
