#ifndef GRAPHVIZHELPER_H
#define GRAPHVIZHELPER_H

#include "graphhelper.h"

namespace vnotex {
class GraphvizHelper : public GraphHelper {
public:
  void update(const QString &p_graphvizFile);

  static GraphvizHelper &getInst();

  static QString getEngineForLanguage(const QString &p_lang);

  static QPair<bool, QString> testGraphviz(const QString &p_graphvizFile);

private:
  GraphvizHelper() = default;

  QStringList getFormatArgs(const QString &p_format) Q_DECL_OVERRIDE;

  QStringList getEngineArgs(const QString &p_engine) const Q_DECL_OVERRIDE;

  static void prepareProgramAndArgs(const QString &p_graphvizFile, QString &p_program,
                                    QStringList &p_args);
};
} // namespace vnotex

#endif // GRAPHVIZHELPER_H
