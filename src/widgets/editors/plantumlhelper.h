#ifndef PLANTUMLHELPER_H
#define PLANTUMLHELPER_H

#include "graphhelper.h"

namespace vnotex
{
    class PlantUmlHelper : public GraphHelper
    {
    public:
        void init(const QString &p_plantUmlJarFile,
                  const QString &p_graphvizFile,
                  const QString &p_overriddenCommand);

        void update(const QString &p_plantUmlJarFile,
                    const QString &p_graphvizFile,
                    const QString &p_overriddenCommand);

        static PlantUmlHelper &getInst()
        {
            static PlantUmlHelper inst;
            return inst;
        }

        static QPair<bool, QString> testPlantUml(const QString &p_plantUmlJarFile);

    private:
        PlantUmlHelper() = default;

        QStringList getFormatArgs(const QString &p_format) Q_DECL_OVERRIDE;

        static void prepareProgramAndArgs(const QString &p_plantUmlJarFile,
                                          const QString &p_graphvizFile,
                                          QString &p_program,
                                          QStringList &p_args);

        bool m_initialized = false;
    };
}

#endif // PLANTUMLHELPER_H
