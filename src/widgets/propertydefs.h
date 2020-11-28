#ifndef PROPERTYDEFS_H
#define PROPERTYDEFS_H

namespace vnotex
{
    // Define properties used for QSS.
    class PropertyDefs
    {
    public:
        PropertyDefs() = delete;

        static const char *s_actionToolButton;

        static const char *s_toolButtonWithoutMenuIndicator;

        static const char *s_dangerousButton;

        // Values: info/warning/error.
        static const char *s_state;
    };
}

#endif // PROPERTYDEFS_H
