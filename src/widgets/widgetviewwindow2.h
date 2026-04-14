#ifndef WIDGETVIEWWINDOW2_H
#define WIDGETVIEWWINDOW2_H

#include "viewwindow2.h"

namespace vnotex {

class IViewWindowContent;

// ViewWindow2 subclass that hosts any IViewWindowContent widget as a tab.
// Used for non-file-backed content like Settings, About, etc.
// Delegates identity, toolbar, dirty state, and lifecycle to the content interface.
class WidgetViewWindow2 : public ViewWindow2 {
  Q_OBJECT
public:
  // @p_services: ServiceLocator for DI.
  // @p_buffer: Virtual buffer handle (from BufferService::openVirtualBuffer).
  // @p_content: IViewWindowContent implementation. Ownership transferred to this window.
  // @p_parent: Parent widget.
  explicit WidgetViewWindow2(ServiceLocator &p_services, const Buffer2 &p_buffer,
                             IViewWindowContent *p_content, QWidget *p_parent = nullptr);

  ~WidgetViewWindow2() override;

  // ============ Identity (delegated to content) ============
  QIcon getIcon() const override;
  QString getName() const override;

  // ============ Pure Virtual Overrides ============
  void setMode(ViewWindowMode p_mode) override;
  QString getLatestContent() const override;

  // ============ Lifecycle ============
  bool aboutToClose(bool p_force) override;

protected:
  void syncEditorFromBuffer() override;
  void setModified(bool p_modified) override;
  void scrollUp() override;
  void scrollDown() override;
  void zoom(bool p_zoomIn) override;

private slots:
  void onContentChanged();

private:
  void setupUI();
  void setupToolBar();

  // NOT owned by QObject parent. Managed via IViewWindowContent::contentWidget().
  IViewWindowContent *m_content = nullptr;
};

} // namespace vnotex

#endif // WIDGETVIEWWINDOW2_H
