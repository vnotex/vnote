#ifndef ACTIVITYSTATSSERVICE_H
#define ACTIVITYSTATSSERVICE_H

#include <QDate>
#include <QObject>
#include <QString>
#include <QVector>

#include "core/noncopyable.h"

#include <vxcore/vxcore_types.h>

namespace vnotex {

// Read-only Qt-side accessor for vxcore's per-device activity.db.
//
// Sibling of the write-only ActivityService: the tracker owns writes, this
// service owns reads. Keeping them separate keeps each responsibility clean and
// gives the dashboard sticker (View) a testable seam that never calls vxcore
// directly (MVC rule).
//
// vxcore date strings are LOCAL 'YYYY-MM-DD', ranges inclusive. Main-thread
// only; the underlying activity queries are cheap and flush implicitly.
class ActivityStatsService : public QObject, private Noncopyable {
  Q_OBJECT

public:
  // Summed metrics for a single local calendar day.
  struct ActivityDay {
    QDate date;
    qint64 activeMs = 0;
    int notesCreated = 0;
    int notesRead = 0;
    int notesEdited = 0;
  };

  // Summed totals plus the per-day series over an inclusive [from, to] range.
  struct ActivityRange {
    QDate from;
    QDate to;
    qint64 activeMs = 0;
    int notesCreated = 0;
    int notesRead = 0;
    int notesEdited = 0;
    QVector<ActivityDay> daily;
  };

  // A file ranked by activity over a range.
  struct HotFile {
    QString notebookId;
    QString fileId;
    QString path;
    int reads = 0;
    int edits = 0;
    double score = 0;
  };

  // p_context: vxcore handle (must outlive this service).
  explicit ActivityStatsService(VxCoreContextHandle p_context, QObject *p_parent = nullptr);

  // Summed totals + per-day series over the inclusive [from, to] range. Returns
  // an empty range (with the requested from/to preserved) on error / null ctx.
  ActivityRange getRange(const QDate &p_from, const QDate &p_to) const;

  // Top files by (reads + edits) over the inclusive [from, to] range, up to
  // p_limit rows. Returns an empty vector on error / null ctx.
  QVector<HotFile> getHotFiles(const QDate &p_from, const QDate &p_to, int p_limit) const;

private:
  VxCoreContextHandle m_context = nullptr;
};

} // namespace vnotex

#endif // ACTIVITYSTATSSERVICE_H
