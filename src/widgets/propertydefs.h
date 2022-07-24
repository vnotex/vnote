#ifndef PROPERTYDEFS_H
#define PROPERTYDEFS_H

namespace vnotex
{
    // Define properties used for QSS.
    class PropertyDefs
    {
    public:
        PropertyDefs() = delete;

        static const char *c_actionToolButton;

        static const char *c_toolButtonWithoutMenuIndicator;

        static const char *c_dangerButton;

        static const char *c_dialogCentralWidget;

        static const char *c_viewSplitCornerWidget;

        static const char *c_viewSplitFlash;

        static const char *c_viewWindowToolBar;

        static const char *c_consoleTextEdit;

        static const char *c_embeddedLineEdit;

        // Values: info/warning/error.
        static const char *c_state;

        static const char *c_dockWidgetIndex;

        static const char *c_dockWidgetTitle;

        static const char *c_hitSettingWidget;

        static const char *c_mainWindowSideBar;
    };
}

#endif // PROPERTYDEFS_H
