#ifndef MINDMAPEDITOR_H
#define MINDMAPEDITOR_H

#include "../webviewer.h"

namespace vnotex
{
    class MindMapEditorAdapter;

    class MindMapEditor : public WebViewer
    {
        Q_OBJECT
    public:
        MindMapEditor(MindMapEditorAdapter *p_adapter,
                      const QColor &p_background,
                      qreal p_zoomFactor,
                      QWidget *p_parent = nullptr);

        MindMapEditorAdapter *adapter() const;

        void setModified(bool p_modified);
        bool isModified() const;

    signals:
        void contentsChanged();

    private:
        // Managed by QObject.
        MindMapEditorAdapter *m_adapter = nullptr;

        bool m_modified = false;
    };
}

#endif // MINDMAPEDITOR_H
