#ifndef PDFANNOTATIONTOOLBAR_H
#define PDFANNOTATIONTOOLBAR_H

#include <functional>

#include <QHash>
#include <QIcon>
#include <QObject>
#include <QString>
#include <QStringList>

#include <core/pdfviewerconfig.h>
#include <gui/utils/commentcolorswatch.h>

class QAction;
class QActionGroup;
class QMenu;
class QToolBar;
class QToolButton;

namespace vnotex {

// Builds and owns the three PDF annotation tool buttons (and their per-tool
// settings menus) on a caller-supplied QToolBar.
//
// Deliberately NOT a QWidget and deliberately NOT holding a ServiceLocator: it
// is constructible in a test with a bare QToolBar and no PdfViewWindow2 / no
// WebEngine profile. That is the whole reason the toolbar was extracted out of
// PdfViewWindow2, whose setupAnnotationToolBarActions() and
// setAuthoringEnabled() are private and unreachable from a test.
//
// It takes the swatch COLOUR RESOLVER (and an icon provider), not a
// ThemeService, so no ThemeService symbol is referenced from this translation
// unit. A default-constructed resolver means the built-in colours.
//
// Each button is a QToolButton::MenuButtonPopup:
//   click the BODY      -> arm/disarm the tool
//   click the INDICATOR -> open that tool's settings menu
//
// Do NOT set the `NoMenuIndicator` dynamic property on these buttons: every
// theme's interface.qss hides the indicator for it, and the indicator is the
// affordance this component is about.
class PdfAnnotationToolBar : public QObject {
  Q_OBJECT
public:
  // Resolves a theme icon file name to a QIcon. Injected for the same
  // link-isolation reason as the colour resolver:
  // ViewWindowToolBarHelper2::generateIcon() needs a ServiceLocator and drags
  // ThemeService in. An empty provider yields icon-less (text-only) actions,
  // which is what the test uses.
  using IconProvider = std::function<QIcon(const QString &p_iconName)>;

  explicit PdfAnnotationToolBar(CommentColorSwatch::ColorResolver p_resolve = {},
                                QString p_borderCss = QString(), QObject *p_parent = nullptr);

  // Creates the actions, buttons and menus on @p_toolBar. Call once.
  void install(QToolBar *p_toolBar, const IconProvider &p_icons = {});

  // Theme switch: re-supply BOTH, then rebuild the menu icons. Constructor-only
  // injection cannot express this — a stored border string outlives a theme
  // change and goes stale.
  void setSwatchResolver(CommentColorSwatch::ColorResolver p_resolve, QString p_borderCss);

  // Disables the actions AND their menus. A read-only file must not be
  // annotatable at all, rather than have each gesture refused after the fact.
  void setAuthoringEnabled(bool p_enabled);

  // Repaints every tick from the caller's state. The adapter (not this
  // component) is the single source of truth, because the web side can disarm a
  // tool by itself.
  void syncState(const QString &p_activeTool,
                 const QHash<QString, PdfViewerConfig::ToolOptions> &p_options);

  QAction *toolAction(const QString &p_tool) const;

  QToolButton *toolButton(const QString &p_tool) const;

  QMenu *toolMenu(const QString &p_tool) const;

  QList<QAction *> toolActions() const;

  // True when the tool group is non-exclusive, i.e. clicking the armed tool
  // disarms it. Exposed so the gate can assert it rather than infer it.
  bool isToolGroupExclusive() const;

signals:
  void toolToggled(const QString &p_tool, bool p_armed);

  void colorPicked(const QString &p_tool, const QString &p_token);

  // Ink width, or free-text font size — whichever scalar the tool carries.
  void scalarPicked(const QString &p_tool, double p_value);

private:
  struct ToolEntry {
    QAction *m_action = nullptr;
    QToolButton *m_button = nullptr;
    QMenu *m_menu = nullptr;
    QActionGroup *m_colorGroup = nullptr;
    QActionGroup *m_scalarGroup = nullptr;
    QList<QAction *> m_colorActions;
    QList<QAction *> m_scalarActions;
  };

  void addTool(QToolBar *p_toolBar, const QString &p_tool, const QString &p_iconName,
               const QString &p_text, const IconProvider &p_icons);

  void buildMenu(ToolEntry &p_entry, const QString &p_tool);

  void rebuildSwatchIcons();

  QIcon swatchIcon(const QString &p_token) const;

  CommentColorSwatch::ColorResolver m_resolve;

  QString m_borderCss;

  // Managed by QObject (this).
  QActionGroup *m_toolGroup = nullptr;

  // Insertion-ordered lookups; the entries' widgets are owned by the toolbar.
  QHash<QString, ToolEntry> m_tools;

  QStringList m_toolOrder;
};

} // namespace vnotex

#endif // PDFANNOTATIONTOOLBAR_H
