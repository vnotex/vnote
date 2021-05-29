#ifndef NONCOPYABLE_H
#define NONCOPYABLE_H

namespace vnotex
{
    class Noncopyable
    {
    protected:
        Noncopyable() = default;

        virtual ~Noncopyable() = default;

        Noncopyable(const Noncopyable&) = delete;
        Noncopyable &operator=(const Noncopyable&) = delete;
    };
}

#endif // NONCOPYABLE_H
