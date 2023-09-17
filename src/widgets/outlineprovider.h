#ifndef OUTLINEPROVIDER_H
#define OUTLINEPROVIDER_H

#include <QObject>
#include <QSharedPointer>
#include <QVector>

#include <limits.h>

namespace vnotex
{
    typedef QVector<int> SectionNumber;

    // Toc content.
    struct Outline
    {
        struct Heading
        {
            Heading() = default;

            Heading(const QString &p_name, int p_level);

            bool operator==(const Heading &p_a) const;

            QString m_name;

            // Heading level, 1-based.
            int m_level = -1;
        };

        void clear();

        bool operator==(const Outline &p_a) const;

        bool isEmpty() const;

        QVector<Heading> m_headings;

        // 1-based.
        // -1 to disable section number by force.
        int m_sectionNumberBaseLevel = 1;

        bool m_sectionNumberEndingDot = true;
    };

    // Used to hold toc-related data of one ViewWindow.
    class OutlineProvider : public QObject
    {
        Q_OBJECT
    public:
        explicit OutlineProvider(QObject *p_parent = nullptr);

        virtual ~OutlineProvider();

        // Get the outline.
        const QSharedPointer<Outline> &getOutline() const;
        void setOutline(const QSharedPointer<Outline> &p_outline);

        // Get current heading index in outline.
        int getCurrentHeadingIndex() const;
        void setCurrentHeadingIndex(int p_idx);

        template <class T>
        static void makePerfectHeadings(const QVector<T> &p_headings, QVector<T> &p_perfectHeadings);

        static void increaseSectionNumber(SectionNumber &p_sectionNumber, int p_level, int p_baseLevel);

        static QString joinSectionNumber(const SectionNumber &p_sectionNumber, bool p_endingDot);

    signals:
        void outlineChanged();

        void currentHeadingChanged();

        void headingClicked(int p_idx);

    private:
        QSharedPointer<Outline> m_outline;

        int m_currentHeadingIndex = -1;
    };

    template <class T>
    void OutlineProvider::makePerfectHeadings(const QVector<T> &p_headings, QVector<T> &p_perfectHeadings)
    {
        p_perfectHeadings.clear();
        if (p_headings.isEmpty()) {
            return;
        }

        int baseLevel = INT_MAX;
        for (const auto &heading : p_headings) {
            if (heading.m_level < baseLevel) {
                baseLevel = heading.m_level;
            }
        }

        p_perfectHeadings.reserve(p_headings.size());
        int curLevel = baseLevel - 1;
        for (const auto &heading : p_headings) {
            while (heading.m_level > curLevel + 1) {
                curLevel += 1;

                // Insert empty level which is an invalid header.
                p_perfectHeadings.append(T(tr("[EMPTY]"), curLevel));
            }

            p_perfectHeadings.append(heading);
            curLevel = heading.m_level;
        }
    }
}

#endif // OUTLINEPROVIDER_H
