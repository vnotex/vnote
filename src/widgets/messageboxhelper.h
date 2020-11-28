#ifndef MESSAGEBOXHELPER_H
#define MESSAGEBOXHELPER_H

#include <QMessageBox>

namespace vnotex
{
    class MessageBoxHelper
    {
    public:
        MessageBoxHelper() = delete;

        enum Type
        {
            Question,
            Information,
            Warning,
            Critical
        };

        // No choice required from user.
        static void notify(MessageBoxHelper::Type p_type,
                           const QString &p_text,
                           const QString &p_informationText = QString(),
                           const QString &p_detailedText = QString(),
                           QWidget *p_parent = nullptr);

        static void notify(MessageBoxHelper::Type p_type,
                           const QString &p_text,
                           QWidget *p_parent);

        // Ask user for OK/Cancel action.
        static int questionOkCancel(MessageBoxHelper::Type p_type,
                                    const QString &p_text,
                                    const QString &p_informationText = QString(),
                                    const QString &p_detailedText = QString(),
                                    QWidget *p_parent = nullptr);

        // Ask user for Yes/No action.
        static int questionYesNo(MessageBoxHelper::Type p_type,
                                 const QString &p_text,
                                 const QString &p_informationText = QString(),
                                 const QString &p_detailedText = QString(),
                                 QWidget *p_parent = nullptr);

        // Ask user for Yes/No/Cancel action.
        static int questionYesNoCancel(MessageBoxHelper::Type p_type,
                                       const QString &p_text,
                                       const QString &p_informationText = QString(),
                                       const QString &p_detailedText = QString(),
                                       QWidget *p_parent = nullptr);

        // Ask user for Save/Discard/Cancel action.
        static int questionSaveDiscardCancel(MessageBoxHelper::Type p_type,
                                             const QString &p_text,
                                             const QString &p_informationText = QString(),
                                             const QString &p_detailedText = QString(),
                                             QWidget *p_parent = nullptr);

    private:
        // Use default title.
        static int showMessageBox(MessageBoxHelper::Type p_type,
                                  const QString &p_text,
                                  const QString &p_informationText,
                                  const QString &p_detailedText,
                                  QMessageBox::StandardButtons p_buttons,
                                  QMessageBox::StandardButton p_defaultButton,
                                  QWidget *p_parent);

        static int showMessageBox(QMessageBox::Icon p_icon,
                                  const QString &p_title,
                                  const QString &p_text,
                                  const QString &p_informationText,
                                  const QString &p_detailedText,
                                  QMessageBox::StandardButtons p_buttons,
                                  QMessageBox::StandardButton p_defaultButton,
                                  QWidget *p_parent);

        static QString TypeTitle(MessageBoxHelper::Type p_type);

        static QMessageBox::Icon TypeIcon(MessageBoxHelper::Type p_type);

    };
} // ns vnotex

#endif // MESSAGEBOXHELPER_H
