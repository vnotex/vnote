#ifndef MINDMAPEDITOR_H
#define MINDMAPEDITOR_H

#include <m3/qt/editor.h>

#include <core/services/buffer2.h>

namespace vnotex {
class MindMapViewWindowController;
class ServiceLocator;

class MindMapEditor : public m3::qt::MindMapEditor {
  Q_OBJECT
public:
  MindMapEditor(ServiceLocator &p_services, const Buffer2 &p_buffer, QWidget *p_parent = nullptr);

  bool loadContent(const QByteArray &p_content);
  // Commits the active draft; throws std::runtime_error rather than returning stale content.
  QString contentForSave();
  void setBuffer(const Buffer2 &p_buffer);
  void applyConfig();
  void setModified(bool p_modified);
  bool isModified() const;

signals:
  void contentsChanged();
  void statusMessageRequested(const QString &p_message);

protected:
  QString resolveDroppedFileUrl(const QString &p_filePath) const override;
  void onAddUrl(const QString &p_nodeId) override;
  void onAddImage(const QString &p_nodeId) override;

private slots:
  void onProtectedLockingChanged(bool p_locking);

private:
  bool canUseBuffer() const;
  bool canEdit() const;
  void markModified();
  void invalidateContent(const QString &p_error);

  ServiceLocator &m_services;
  Buffer2 m_buffer;
  // Owned by this widget; all resource and link policy stays in the controller.
  MindMapViewWindowController *m_controller = nullptr;
  quint64 m_generation = 0;
  QString m_loadError;
  bool m_loading = false;
  bool m_contentLoaded = false;
  bool m_modified = false;
};
} // namespace vnotex

#endif // MINDMAPEDITOR_H
