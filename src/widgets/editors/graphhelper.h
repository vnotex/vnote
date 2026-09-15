#ifndef GRAPHHELPER_H
#define GRAPHHELPER_H

#include <QPair>
#include <QPointer>
#include <QProcess>
#include <QQueue>
#include <QStringList>

#include <core/global.h>
#include <core/noncopyable.h>
#include <vtextedit/lrucache.h>

namespace vnotex {
class GraphHelper : private Noncopyable {
public:
  typedef std::function<void(quint64, TimeStamp, const QString &, const QString &, bool)>
      ResultCallback;

  GraphHelper();

  void process(quint64 p_id, TimeStamp p_timeStamp, const QString &p_format, const QString &p_text,
               QObject *p_owner, const ResultCallback &p_callback, int p_imageIndex = 0,
               const QString &p_engine = QString());

protected:
  virtual QStringList getFormatArgs(const QString &p_format) = 0;

  virtual QStringList getImageArgs(int p_imageIndex) const;

  virtual QStringList getEngineArgs(const QString &p_engine) const;

  void clearCache();

  void checkValidProgram();

  static QStringList getArgsToUse(const QStringList &p_args);

  static QString getCommandToUse(const QString &p_command, const QString &p_format);

  QString m_program;

  QStringList m_args;

  // If this is not empty, @m_program and @m_args will be ignored.
  QString m_overriddenCommand;

private:
  struct Task {
    quint64 m_id = 0;

    TimeStamp m_timeStamp = 0;

    QString m_format;

    QString m_text;

    int m_imageIndex = 0;

    QString m_engine;

    QPointer<QObject> m_owner;

    ResultCallback m_callback;
  };

  struct CacheItem {
    bool isNull() const { return m_format.isEmpty(); }

    QString m_format;

    QString m_data;
  };

  void processOneTask();

  void finishOneTask(QProcess *p_process, int p_exitCode, QProcess::ExitStatus p_exitStatus);

  void finishOneTask(const QString &p_data);

  void callbackOneTask(const Task &p_task, quint64 p_id, TimeStamp p_timeStamp,
                       const QString &p_format, const QString &p_data, bool p_success) const;

  QQueue<Task> m_tasks;

  bool m_taskOngoing = false;

  // {text, {image index, engine}} -> CacheItem.
  vte::LruCache<QPair<QString, QPair<int, QString>>, CacheItem> m_cache;

  // Whether @m_program is valid.
  bool m_programValid = false;
};
} // namespace vnotex

#endif // GRAPHHELPER_H
