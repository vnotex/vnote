#include "messageboxhelper.h"

#include <QObject>

using namespace vnotex;

QString MessageBoxHelper::TypeTitle(MessageBoxHelper::Type p_type)
{
    QString title;
    switch (p_type) {
    case Question:
        title = QMessageBox::tr("Question");
        break;

    case Information:
        title = QMessageBox::tr("Information");
        break;

    case Warning:
        title = QMessageBox::tr("Warning");
        break;

    case Critical:
        title = QMessageBox::tr("Critical");
        break;
    }

    return title;
}

QMessageBox::Icon MessageBoxHelper::TypeIcon(MessageBoxHelper::Type p_type)
{
    auto icon = QMessageBox::NoIcon;
    switch (p_type) {
    case Question:
        icon = QMessageBox::Question;
        break;

    case Information:
        icon = QMessageBox::Information;
        break;

    case Warning:
        icon = QMessageBox::Warning;
        break;

    case Critical:
        icon = QMessageBox::Critical;
        break;
    }

    return icon;
}

int MessageBoxHelper::showMessageBox(MessageBoxHelper::Type p_type,
                               const QString &p_text,
                               const QString &p_informationText,
                               const QString &p_detailedText,
                               QMessageBox::StandardButtons p_buttons,
                               QMessageBox::StandardButton p_defaultButton,
                               QWidget *p_parent)
{
    return showMessageBox(TypeIcon(p_type),
                          TypeTitle(p_type),
                          p_text,
                          p_informationText,
                          p_detailedText,
                          p_buttons,
                          p_defaultButton,
                          p_parent);
}

int MessageBoxHelper::showMessageBox(QMessageBox::Icon p_icon,
                               const QString &p_title,
                               const QString &p_text,
                               const QString &p_informationText,
                               const QString &p_detailedText,
                               QMessageBox::StandardButtons p_buttons,
                               QMessageBox::StandardButton p_defaultButton,
                               QWidget *p_parent)
{
    QMessageBox msgBox(p_icon, p_title, p_text, p_buttons, p_parent);
    msgBox.setTextInteractionFlags(msgBox.textInteractionFlags()
                                   | Qt::TextSelectableByMouse);
    msgBox.setInformativeText(p_informationText);
    msgBox.setDetailedText(p_detailedText);
    msgBox.setDefaultButton(p_defaultButton);
    return msgBox.exec();
}

void MessageBoxHelper::notify(MessageBoxHelper::Type p_type,
                              const QString &p_text,
                              const QString &p_informationText,
                              const QString &p_detailedText,
                              QWidget *p_parent)
{
    showMessageBox(p_type,
                   p_text,
                   p_informationText,
                   p_detailedText,
                   QMessageBox::Ok,
                   QMessageBox::Ok,
                   p_parent);
}

void MessageBoxHelper::notify(MessageBoxHelper::Type p_type,
                              const QString &p_text,
                              QWidget *p_parent)
{
    showMessageBox(p_type,
                   p_text,
                   QString(),
                   QString(),
                   QMessageBox::Ok,
                   QMessageBox::Ok,
                   p_parent);
}

int MessageBoxHelper::questionOkCancel(MessageBoxHelper::Type p_type,
                                       const QString &p_text,
                                       const QString &p_informationText,
                                       const QString &p_detailedText,
                                       QWidget *p_parent)
{
    int ret = showMessageBox(p_type,
                             p_text,
                             p_informationText,
                             p_detailedText,
                             QMessageBox::Ok | QMessageBox::Cancel,
                             QMessageBox::Ok,
                             p_parent);
    return ret;
}

int MessageBoxHelper::questionYesNo(MessageBoxHelper::Type p_type,
                                    const QString &p_text,
                                    const QString &p_informationText,
                                    const QString &p_detailedText,
                                    QWidget *p_parent)
{
    int ret = showMessageBox(p_type,
                             p_text,
                             p_informationText,
                             p_detailedText,
                             QMessageBox::Yes | QMessageBox::No,
                             QMessageBox::Yes,
                             p_parent);
    return ret;
}

int MessageBoxHelper::questionYesNoCancel(MessageBoxHelper::Type p_type,
                                          const QString &p_text,
                                          const QString &p_informationText,
                                          const QString &p_detailedText,
                                          QWidget *p_parent)
{
    int ret = showMessageBox(p_type,
                             p_text,
                             p_informationText,
                             p_detailedText,
                             QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
                             QMessageBox::Yes,
                             p_parent);
    return ret;
}

int MessageBoxHelper::questionSaveDiscardCancel(MessageBoxHelper::Type p_type,
                                                const QString &p_text,
                                                const QString &p_informationText,
                                                const QString &p_detailedText,
                                                QWidget *p_parent)
{
    int ret = showMessageBox(p_type,
                             p_text,
                             p_informationText,
                             p_detailedText,
                             QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                             QMessageBox::Save,
                             p_parent);
    return ret;
}
