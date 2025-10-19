/*
MIT License

Copyright (c) Igor Korsukov

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

#include <algorithm>

#include "../conf.h"
#include "queuepool.h"

namespace kors::async {
QueuePool* QueuePool::instance()
{
    static QueuePool p;
    return &p;
}

QueuePool::QueuePool()
    : m_threads{MAX_THREADS, nullptr}
{
}

QueuePool::~QueuePool()
{
    for (ThreadData* td : m_threads) {
        delete td;
    }
}

void QueuePool::regPort(const std::thread::id& th, const std::shared_ptr<Port>& port)
{
    bool found = false;
    for (size_t i = 0; i < m_threads.size(); ++i) {
        ThreadData* thdata = m_threads.at(i);
        // new slot
        if (!thdata) {
            std::scoped_lock lock(m_mutex);
            thdata = m_threads.at(i);
            if (thdata) {
                // someone managed to take it
                // will try to register again
                regPort(th, port);
                return;
            }

            thdata = new ThreadData();
            thdata->threadId = th;
            thdata->ports.push_back(port);
            m_threads[i] = thdata;
            ++m_count;
            found = true;
            break;
        } else
        // found a slot for the given thread
        if (thdata->threadId == th) {
            thdata->locked = true;
            thdata->ports.push_back(port);
            thdata->locked = false;
            found = true;
            break;
        } else
        // found a slot that has no ports (unregistered)
        if (thdata->ports.empty()) {
            thdata->locked = true;
            thdata->threadId = th;
            thdata->ports.push_back(port);
            thdata->locked = false;
            found = true;
            break;
        }
    }

    assert(found && "thread pool exhausted");
}

void QueuePool::unregPort(const std::thread::id& th, const std::shared_ptr<Port>& port)
{
    for (size_t i = 0; i < m_threads.size(); ++i) {
        ThreadData* thdata = m_threads.at(i);
        if (!thdata) {
            break;
        } else if (thdata->threadId == th) {
            thdata->locked = true;
            auto& ports = thdata->ports;
            ports.erase(std::remove(ports.begin(), ports.end(), port), ports.end());
            thdata->locked = false;
        }
    }
}

void QueuePool::processMessages()
{
    std::thread::id threadId = std::this_thread::get_id();
    processMessages(threadId);
}

void QueuePool::processMessages(const std::thread::id& th)
{
    size_t count = m_count.load();
    assert(count <= m_threads.size());
    for (size_t i = 0; i < count; ++i) {
        ThreadData* thdata = m_threads.at(i);
        assert(thdata);
        if (!thdata) {
            break;
        } else if (thdata->threadId == th) {
            if (thdata->locked) {
                break;
            }
            for (auto& p : thdata->ports) {
                p->process();
            }
            break;
        }
    }
}
} // kors::async
