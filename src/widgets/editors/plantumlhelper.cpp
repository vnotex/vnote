#include "plantumlhelper.h"

#include <QDebug>
#include <QDir>

#include <core/configmgr.h>
#include <core/editorconfig.h>
#include <core/markdowneditorconfig.h>
#include <utils/pathutils.h>
#include <utils/processutils.h>

using namespace vnotex;

PlantUmlHelper &PlantUmlHelper::getInst() {
  static bool initialized = false;
  static PlantUmlHelper inst;
  if (!initialized) {
    initialized = true;
    const auto &markdownEditorConfig =
        ConfigMgr::getInst().getEditorConfig().getMarkdownEditorConfig();
    inst.update(markdownEditorConfig.getPlantUmlJar(), markdownEditorConfig.getGraphvizExe(),
                markdownEditorConfig.getPlantUmlCommand());
  }
  return inst;
}

void PlantUmlHelper::update(const QString &p_plantUmlJarFile, const QString &p_graphvizFile,
                            const QString &p_overriddenCommand) {
  m_overriddenCommand = p_overriddenCommand;
  if (m_overriddenCommand.isEmpty()) {
    prepareProgramAndArgs(p_plantUmlJarFile, p_graphvizFile, m_program, m_args);
  } else {
    m_program.clear();
    m_args.clear();
  }

  checkValidProgram();

  clearCache();
}

void PlantUmlHelper::prepareProgramAndArgs(const QString &p_plantUmlJarFile,
                                           const QString &p_graphvizFile, QString &p_program,
                                           QStringList &p_args) {
  p_program.clear();
  p_args.clear();

#if defined(Q_OS_WIN)
  p_program = "java";
#else
  p_program = "/bin/sh";
  p_args << "-c";
  p_args << "java";
#endif

#if defined(Q_OS_MACOS)
  p_args << "-Djava.awt.headless=true";
#endif

  p_args << "-jar" << QDir::toNativeSeparators(PathUtils::absolutePath(p_plantUmlJarFile));

  p_args << "-charset" << "UTF-8";

  if (!p_graphvizFile.isEmpty()) {
    p_args << "-graphvizdot" << QDir::toNativeSeparators(PathUtils::absolutePath(p_graphvizFile));
  }

  p_args << "-pipe";
}

QPair<bool, QString> PlantUmlHelper::testPlantUml(const QString &p_plantUmlJarFile) {
  auto ret = qMakePair(false, QString());

  QString program;
  QStringList args;
  prepareProgramAndArgs(p_plantUmlJarFile, QString(), program, args);

  args << "-tsvg";
  args = getArgsToUse(args);

  const QString testGraph("VNote->Markdown : Hello");

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

QStringList PlantUmlHelper::getFormatArgs(const QString &p_format) {
  QStringList args;
  args << ("-t" + p_format);
  return args;
}
