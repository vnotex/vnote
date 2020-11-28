#include "webviewer.h"

using namespace vnotex;

WebViewer::WebViewer(QWidget *p_parent)
    : QWebEngineView(p_parent)
{
    setAcceptDrops(false);
}

WebViewer::~WebViewer()
{

}
