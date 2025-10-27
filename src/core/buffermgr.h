#ifndef BUFFERMGR_H
#define BUFFERMGR_H

#include <QMap>
#include <QObject>
#include <QScopedPointer>
#include <QSharedPointer>
#include <QVector>

#include "coreconfig.h"
#include "namebasedserver.h"

namespace vnotex {
class IBufferFactory;
class Node;
class Buffer;
struct FileOpenParameters;

class BufferMgr : public QObject {
  Q_OBJECT
public:
  explicit BufferMgr(QObject *p_parent = nullptr);

  ~BufferMgr();

  void init();

public slots:
  void open(Node *p_node, const QSharedPointer<FileOpenParameters> &p_paras);

  void open(const QString &p_filePath, const QSharedPointer<FileOpenParameters> &p_paras);

  static void updateSuffixToFileType(const QVector<CoreConfig::FileTypeSuffix> &p_fileTypeSuffixes);

signals:
  void bufferRequested(Buffer *p_buffer, const QSharedPointer<FileOpenParameters> &p_paras);

private:
  void initBufferServer();

  Buffer *findBuffer(const Node *p_node) const;

  Buffer *findBuffer(const QString &p_filePath) const;

  void addBuffer(Buffer *p_buffer);

  bool openWithExternalProgram(const QString &p_filePath, const QString &p_name) const;

  bool isSameTypeBuffer(const Buffer *p_buffer, const QString &p_typeName) const;

  static QString findFileTypeByFile(const QString &p_filePath);

  QSharedPointer<NameBasedServer<IBufferFactory>> m_bufferServer;

  // Managed by QObject.
  QVector<Buffer *> m_buffers;

  // Mapping from suffix to file type or external program name.
  static QMap<QString, QString> s_suffixToFileType;
};
} // namespace vnotex

#endif // BUFFERMGR_H
