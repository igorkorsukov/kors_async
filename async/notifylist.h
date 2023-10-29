/*
MIT License

Copyright (c) 2020 Igor Korsukov

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#ifndef KORS_ASYNC_NOTIFYLIST_H
#define KORS_ASYNC_NOTIFYLIST_H

#include <vector>
#include "changednotify.h"

namespace kors::async {
template<typename T>
class NotifyList : public std::vector<T>
{
public:
    NotifyList() {}
    NotifyList(const NotifyList&) = default;
    NotifyList(std::shared_ptr<ChangedNotify<T> > n)
        : m_notify(n) {}
    NotifyList(const std::vector<T>& l, std::shared_ptr<ChangedNotify<T> > n)
        : std::vector<T>(l), m_notify(n) {}

    void setNotify(std::shared_ptr<ChangedNotify<T> > n)
    {
        m_notify = n;
    }

    NotifyList<T>& operator =(const NotifyList<T>& nl)
    {
        std::vector<T>::operator=(nl);
        m_notify = nl.m_notify;
        return *this;
    }

    template<typename Call>
    void onChanged(Asyncable* caller, Call f, Asyncable::AsyncMode mode = Asyncable::AsyncMode::AsyncSetOnce)
    {
        Q_ASSERT(m_notify);
        if (!m_notify) {
            return;
        }
        m_notify->onChanged(caller, f, mode);
    }

    void resetOnChanged(Asyncable* caller)
    {
        Q_ASSERT(m_notify);
        if (!m_notify) {
            return;
        }
        m_notify->resetOnChanged(caller);
    }

    template<typename Call>
    void onItemChanged(Asyncable* caller, Call f, Asyncable::AsyncMode mode = Asyncable::AsyncMode::AsyncSetOnce)
    {
        Q_ASSERT(m_notify);
        if (!m_notify) {
            return;
        }
        m_notify->onItemChanged(caller, f, mode);
    }

    void resetOnItemChanged(Asyncable* caller)
    {
        Q_ASSERT(m_notify);
        if (!m_notify) {
            return;
        }
        m_notify->resetOnItemChanged(caller);
    }

    template<typename Call>
    void onItemAdded(Asyncable* caller, Call f, Asyncable::AsyncMode mode = Asyncable::AsyncMode::AsyncSetOnce)
    {
        Q_ASSERT(m_notify);
        if (!m_notify) {
            return;
        }
        m_notify->onItemAdded(caller, f, mode);
    }

    void resetOnItemAdded(Asyncable* caller)
    {
        Q_ASSERT(m_notify);
        if (!m_notify) {
            return;
        }
        m_notify->resetOnItemAdded(caller);
    }

    template<typename Call>
    void onItemRemoved(Asyncable* caller, Call f, Asyncable::AsyncMode mode = Asyncable::AsyncMode::AsyncSetOnce)
    {
        Q_ASSERT(m_notify);
        if (!m_notify) {
            return;
        }
        m_notify->onItemRemoved(caller, f, mode);
    }

    void resetOnItemRemoved(Asyncable* caller)
    {
        m_notify->resetOnItemRemoved(caller);
    }

    template<typename Call>
    void onItemReplaced(Asyncable* caller, Call f, Asyncable::AsyncMode mode = Asyncable::AsyncMode::AsyncSetOnce)
    {
        Q_ASSERT(m_notify);
        if (!m_notify) {
            return;
        }
        m_notify->onItemReplaced(caller, f, mode);
    }

    void resetOnItemReplaced(Asyncable* caller)
    {
        m_notify->resetOnItemReplaced(caller);
    }

private:
    std::shared_ptr<ChangedNotify<T> > m_notify = nullptr;
};
}

#endif // KORS_ASYNC_NOTIFYLIST_H
