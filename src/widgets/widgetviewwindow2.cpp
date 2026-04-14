#include "widgetviewwindow2.h"

#include <QMessageBox>
#include <QToolBar>

#include <core/iviewwindowcontent.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>

#include "messageboxhelper.h"

using namespace vnotex;

WidgetViewWindow2::WidgetViewWindow2(ServiceLocator &p_services, const Buffer2 &p_buffer,
                                     IViewWindowContent *p_content, QWidget *p_parent)
    : ViewWindow2(p_services, p_buffer, p_parent), m_content(p_content) {
  Q_ASSERT(m_content);
  Q_ASSERT(m_content->contentWidget());

  m_mode = ViewWindowMode::Read;

  setupUI();
  setupToolBar();

  m_editorDirty = m_content->isDirty();

  connect(m_content->contentWidget(), SIGNAL(contentChanged()), this, SLOT(onContentChanged()));
}

WidgetViewWindow2::~WidgetViewWindow2() = default;

QIcon WidgetViewWindow2::getIcon() const { return m_content ? m_content->icon() : QIcon(); }

QString WidgetViewWindow2::getName() const { return m_content ? m_content->title() : QString(); }

void WidgetViewWindow2::setMode(ViewWindowMode p_mode) { Q_UNUSED(p_mode); }

QString WidgetViewWindow2::getLatestContent() const { return QString(); }

bool WidgetViewWindow2::aboutToClose(bool p_force) {
  if (!m_content) {
    return true;
  }

  if (p_force) {
    m_content->reset();
    setModified(false);
  } else if (m_content->isDirty()) {
    int ret = MessageBoxHelper::questionSaveDiscardCancel(
        MessageBoxHelper::Question, tr("Do you want to save changes to \"%1\"?").arg(getName()),
        tr("Your changes will be lost if you don't save them."), QString(), this);

    if (ret == QMessageBox::Cancel) {
      return false;
    }

    if (ret == QMessageBox::Save) {
      static const int c_maxRetries = 3;
      bool done = false;
      for (int attempt = 0; attempt < c_maxRetries; ++attempt) {
        if (m_content->save()) {
          setModified(m_content->isDirty());
          done = true;
          break;
        }

        QMessageBox::StandardButtons buttons =
            (attempt < c_maxRetries - 1)
                ? (QMessageBox::Retry | QMessageBox::Discard | QMessageBox::Cancel)
                : (QMessageBox::Discard | QMessageBox::Cancel);
        QMessageBox msgBox(QMessageBox::Warning, tr("Save Changes"),
                           tr("Failed to save \"%1\".").arg(getName()), buttons, this);
        msgBox.setInformativeText((attempt < c_maxRetries - 1)
                                      ? tr("Would you like to retry, discard changes, or cancel?")
                                      : tr("Maximum retries reached. Discard changes or cancel?"));
        msgBox.setDefaultButton((attempt < c_maxRetries - 1) ? QMessageBox::Retry
                                                             : QMessageBox::Cancel);
        int failRet = msgBox.exec();

        if (failRet == QMessageBox::Cancel) {
          return false;
        }

        if (failRet == QMessageBox::Discard) {
          m_content->reset();
          setModified(false);
          done = true;
          break;
        }
      }

      if (!done) {
        return false;
      }
    } else {
      m_content->reset();
      setModified(false);
    }
  }

  if (!m_content->canClose(p_force)) {
    return false;
  }

  const auto &buffer = getBuffer();
  if (buffer.isValid()) {
    auto *bufferService = getServices().get<BufferService>();
    if (bufferService) {
      bufferService->unregisterActiveWriter(buffer.id(), reinterpret_cast<quintptr>(this));
    }
  }

  return true;
}

void WidgetViewWindow2::syncEditorFromBuffer() {}

void WidgetViewWindow2::setModified(bool p_modified) {
  if (m_editorDirty == p_modified) {
    return;
  }

  m_editorDirty = p_modified;
  emit statusChanged();
}

void WidgetViewWindow2::scrollUp() {}

void WidgetViewWindow2::scrollDown() {}

void WidgetViewWindow2::zoom(bool p_zoomIn) { Q_UNUSED(p_zoomIn); }

void WidgetViewWindow2::onContentChanged() {
  m_editorDirty = m_content && m_content->isDirty();
  emit statusChanged();
  emit nameChanged();
}

void WidgetViewWindow2::setupUI() {
  setCentralWidget(m_content ? m_content->contentWidget() : nullptr);
}

void WidgetViewWindow2::setupToolBar() {
  auto *toolBar = createToolBar(this);
  if (m_content) {
    m_content->setupToolBar(toolBar);
  }
  addToolBar(toolBar);
}
