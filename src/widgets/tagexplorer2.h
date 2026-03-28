#ifndef TAGEXPLORER2_H
#define TAGEXPLORER2_H

#include <QFrame>

#include <core/nodeidentifier.h>
#include <core/noncopyable.h>

class QSplitter;

namespace vnotex {

class ServiceLocator;
class TitleBar;
class TagModel;
class TagView;
class TagController;
class TagFileModel;
class FileListView;
class FileNodeDelegate;

// TagExplorer2 is a dock shell widget that wires together TagModel, TagView,
// FileListView (for tag-matched files), and TagController into a QFrame with
// TitleBar and QSplitter.
class TagExplorer2 : public QFrame, private Noncopyable {
  Q_OBJECT

public:
  explicit TagExplorer2(ServiceLocator &p_services, QWidget *p_parent = nullptr);
  ~TagExplorer2() override = default;

  void setNotebookId(const QString &p_notebookId);

  QByteArray saveState() const;
  void restoreState(const QByteArray &p_data);

signals:
  void openNodeRequested(const vnotex::NodeIdentifier &p_nodeId);

private slots:
  void onNewTagRequested(const QString &p_notebookId);
  void onDeleteTagRequested(const QString &p_notebookId, const QString &p_tagName);
  void onErrorOccurred(const QString &p_title, const QString &p_message);
  void onContextMenuRequested(const QString &p_tagName, const QPoint &p_globalPos);
  void onMatchingNodesChanged(const QJsonArray &p_nodes);

private:
  void setupUI();
  void setupTitleBar();
  void setupTitleBarMenu();

  ServiceLocator &m_services;

  TitleBar *m_titleBar = nullptr;
  QSplitter *m_splitter = nullptr;
  TagModel *m_tagModel = nullptr;
  TagView *m_tagView = nullptr;
  TagController *m_tagController = nullptr;

  // File list panel
  TagFileModel *m_fileModel = nullptr;
  FileListView *m_fileView = nullptr;
  FileNodeDelegate *m_fileDelegate = nullptr;

  QString m_notebookId;
};

} // namespace vnotex

#endif // TAGEXPLORER2_H
