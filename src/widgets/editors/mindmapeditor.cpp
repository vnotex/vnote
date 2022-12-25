#include "mindmapeditor.h"

#include <QWebChannel>

#include "mindmapeditoradapter.h"

using namespace vnotex;

MindMapEditor::MindMapEditor(MindMapEditorAdapter *p_adapter,
                            const QColor &p_background,
                            qreal p_zoomFactor,
                            QWidget *p_parent)
    : WebViewer(p_background, p_zoomFactor, p_parent),
      m_adapter(p_adapter)
{
    setAcceptDrops(true);

    m_adapter->setParent(this);
    connect(m_adapter, &MindMapEditorAdapter::contentsChanged,
            this, [this]() {
                m_modified = true;
                emit contentsChanged();
            });

    auto channel = new QWebChannel(this);
    channel->registerObject(QStringLiteral("vxAdapter"), m_adapter);

    page()->setWebChannel(channel);
}

MindMapEditorAdapter *MindMapEditor::adapter() const
{
    return m_adapter;
}

void MindMapEditor::setModified(bool p_modified)
{
    m_modified = p_modified;
}

bool MindMapEditor::isModified() const
{
    return m_modified;
}
