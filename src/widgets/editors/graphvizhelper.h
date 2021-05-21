#ifndef GRAPHVIZHELPER_H
#define GRAPHVIZHELPER_H

#include "graphhelper.h"

namespace vnotex
{
    class GraphvizHelper : public GraphHelper
    {
    public:
        void init(const QString &p_graphvizFile);

        void update(const QString &p_graphvizFile);

        static GraphvizHelper &getInst()
        {
            static GraphvizHelper inst;
            return inst;
        }

        static QPair<bool, QString> testGraphviz(const QString &p_graphvizFile);

    private:
        GraphvizHelper() = default;

        QStringList getFormatArgs(const QString &p_format) Q_DECL_OVERRIDE;

        static void prepareProgramAndArgs(const QString &p_graphvizFile,
                                          QString &p_program,
                                          QStringList &p_args);

        bool m_initialized = false;
    };
}

#endif // GRAPHVIZHELPER_H
