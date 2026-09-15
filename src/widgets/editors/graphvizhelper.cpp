#include "graphvizhelper.h"

#include <QDebug>
#include <QDir>

#include <utils/pathutils.h>
#include <utils/processutils.h>

using namespace vnotex;

GraphvizHelper &GraphvizHelper::getInst() {
  static bool initialized = false;
  static GraphvizHelper inst;
  if (!initialized) {
    initialized = true;
    // Seed with the default program; the owning view window re-configures the
    // resolved Graphviz path via update() using ConfigMgr2.
    inst.update(QString());
  }
  return inst;
}

QString GraphvizHelper::getEngineForLanguage(const QString &p_lang) {
  if (p_lang == QStringLiteral("graphviz")) {
    return QStringLiteral("dot");
  }
  static const QStringList engines = {QStringLiteral("dot"),       QStringLiteral("neato"),
                                      QStringLiteral("twopi"),     QStringLiteral("circo"),
                                      QStringLiteral("fdp"),       QStringLiteral("sfdp"),
                                      QStringLiteral("patchwork"), QStringLiteral("osage")};
  return engines.contains(p_lang) ? p_lang : QString();
}

QStringList GraphvizHelper::getEngineArgs(const QString &p_engine) const {
  return p_engine.isEmpty() ? QStringList() : QStringList{QStringLiteral("-K") + p_engine};
}

void GraphvizHelper::update(const QString &p_graphvizFile) {
  prepareProgramAndArgs(p_graphvizFile, m_program, m_args);

  checkValidProgram();

  clearCache();
}

void GraphvizHelper::prepareProgramAndArgs(const QString &p_graphvizFile, QString &p_program,
                                           QStringList &p_args) {
  p_program.clear();
  p_args.clear();

  p_program = p_graphvizFile.isEmpty()
                  ? QStringLiteral("dot")
                  : QDir::toNativeSeparators(PathUtils::absolutePath(p_graphvizFile));
}

QPair<bool, QString> GraphvizHelper::testGraphviz(const QString &p_graphvizFile) {
  auto ret = qMakePair(false, QString());

  QString program;
  QStringList args;
  prepareProgramAndArgs(p_graphvizFile, program, args);
  args << "-Tsvg";

  const QString testGraph("digraph G {VNote->Markdown}");

  int exitCode = -1;
  QByteArray outData;
  QByteArray errData;
  auto state = ProcessUtils::start(program, args, testGraph.toUtf8(), exitCode, outData, errData);
  ret.first = (state == ProcessUtils::Succeeded) && (exitCode == 0);

  ret.second = QStringLiteral("%1 %2\n\nExitcode: %3\n\nOutput: %4\n\nError: %5")
                   .arg(program, args.join(' '), QString::number(exitCode),
                        QString::fromLocal8Bit(outData), QString::fromLocal8Bit(errData));

  return ret;
}

QStringList GraphvizHelper::getFormatArgs(const QString &p_format) {
  QStringList args;
  args << ("-T" + p_format);
  return args;
}
