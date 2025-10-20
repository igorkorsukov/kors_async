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
#pragma once

#include <functional>
#include <thread>
#include <vector>
#include <cassert>
#include <algorithm>
#include <atomic>

#include "../conf.h"
#include "../asyncable.h"
#include "queuepool.h"

namespace kors::async {
enum class SendMode {
    Auto = 0,
    Queue
};

template<typename ... T>
class ChannelImpl : public Asyncable::IConnectable
{
public:
    using Callback = std::function<void (const T&...)>;

private:

    struct Receiver {
        bool enabled = true;
        Asyncable* receiver = nullptr;
        Callback callback;
    };

    struct QueueData {
        std::thread::id receiveTh;
        Queue queue;
        QueueData()
            : queue(QUEUE_CAPACITY) {}
    };

    struct ThreadData {
        std::thread::id threadId;
        bool receiversIteration = false;
        std::vector<Receiver*> receivers;
        std::vector<QueueData*> queues;
    };

    std::vector<ThreadData*> m_threads;
    std::atomic<int> m_enabledReceiversCount = 0;

    ThreadData& threadData(const std::thread::id& thId)
    {
        for (size_t i = 0; i < m_threads.size(); ++i) {
            ThreadData* thdata = m_threads.at(i);
            if (!thdata) {
                thdata = new ThreadData();
                thdata->threadId = thId;
                m_threads[i] = thdata;
                return *m_threads[i];
            } else if (thdata->threadId == thId) {
                return *m_threads[i];
            }
        }

        assert(false && "thread pool exhausted");
        static ThreadData dummy;
        return dummy;
    }

    inline auto findReceiver(const std::vector<Receiver*>& receivers, const Asyncable* a) const
    {
        auto it = std::find_if(receivers.begin(), receivers.end(), [a](const Receiver* r) {
            return a == r->receiver;
        });
        return it;
    }

    // IConnectable
    void disconnectAsyncable(Asyncable* a, const std::thread::id& connectThId) override
    {
        bool ok = disconnectReceiver(a, connectThId);
        if (!ok) {
            disableReceiver(a, connectThId);
        }
    }

    void sendToQueue(ThreadData& sendThdata, const std::thread::id& receiveTh, const CallMsg& msg)
    {
        assert(sendThdata.threadId == std::this_thread::get_id());

        // we are looking queue for the receiver.
        QueueData* qdata = nullptr;
        for (QueueData* qd : sendThdata.queues) {
            if (qd->receiveTh == receiveTh) {
                qdata = qd;
                break;
            }
        }

        // we'll create a new one if we didn't find one.
        if (!qdata) {
            qdata = new QueueData();
            qdata->receiveTh = receiveTh;
            qdata->queue.port2()->onMessage([this](const CallMsg& m) {
                const std::thread::id threadId = std::this_thread::get_id();
                ThreadData& thdata = threadData(threadId);
                thdata.receiversIteration = true;
                for (const Receiver* r : thdata.receivers) {
                    if (!r->enabled) {
                        continue;
                    }

                    m.func(r);
                }
                thdata.receiversIteration = false;
            });

            QueuePool::instance()->regPort(sendThdata.threadId, qdata->queue.port1());  // send
            QueuePool::instance()->regPort(receiveTh, qdata->queue.port2());            // receive

            sendThdata.queues.push_back(qdata);
        }

        qdata->queue.port1()->send(msg);
    }

    void unregAllQueue()
    {
        QueuePool* pool = QueuePool::instance();
        for (ThreadData* thdata : m_threads) {
            if (!thdata) {
                break;
            }

            for (QueueData* qdata : thdata->queues) {
                pool->unregPort(thdata->threadId, qdata->queue.port1()); // send
                pool->unregPort(qdata->receiveTh, qdata->queue.port2()); // receive
            }
        }
    }

    void sendAuto(const T&... args)
    {
        const std::thread::id threadId = std::this_thread::get_id();

        ThreadData& sendThdata = threadData(threadId);

        // the sender's thread is the same as the receiver's thread
        sendThdata.receiversIteration = true;
        for (const Receiver* r : sendThdata.receivers) {
            if (r->enabled) {
                r->callback(args ...);
            }
        }
        sendThdata.receiversIteration = false;

        // we send messages to call in a thread of other receivers
        for (ThreadData* receiveThdata : m_threads) {
            if (!receiveThdata) {
                // there is no one further
                break;
            }

            if (receiveThdata->threadId == threadId) {
                // skip this thread
                continue;
            }

            CallMsg msg;
            msg.func = [args ...](const void* r) {
                reinterpret_cast<const Receiver*>(r)->callback(args ...);
            };
            sendToQueue(sendThdata, receiveThdata->threadId, msg);
        }
    }

    void sendQueue(const T&... args)
    {
        const std::thread::id threadId = std::this_thread::get_id();

        ThreadData& sendThdata = threadData(threadId);

        for (ThreadData* receiveThdata : m_threads) {
            if (!receiveThdata) {
                // there is no one further
                break;
            }

            CallMsg msg;
            msg.func = [args ...](const void* r) {
                reinterpret_cast<const Receiver*>(r)->callback(args ...);
            };
            sendToQueue(sendThdata, receiveThdata->threadId, msg);
        }
    }

public:

    ChannelImpl(size_t max_threads = MAX_THREADS_PER_CHANNEL)
        : m_threads{max_threads, nullptr} {}

    ChannelImpl(const ChannelImpl&) = delete;
    ChannelImpl& operator=(const ChannelImpl&) = delete;

    ~ChannelImpl()
    {
        unregAllQueue();

        for (ThreadData* thdata : m_threads) {
            if (!thdata) {
                break;
            }

            for (Receiver* r : thdata->receivers) {
                if (r->receiver) {
                    r->receiver->async_disconnect(this);
                }
                delete r;
            }

            for (QueueData* qdata : thdata->queues) {
                delete qdata;
            }
        }
    }

    void send(SendMode mode, const T&... args)
    {
        if (!isConnected()) {
            return;
        }

        switch (mode) {
        case SendMode::Auto: {
            sendAuto(args ...);
        } break;
        case SendMode::Queue: {
            sendQueue(args ...);
        } break;
        }
    }

    bool isInsideIteration(const std::thread::id threadId = std::this_thread::get_id()) const
    {
        ThreadData& thdata = threadData(threadId);
        return thdata.receiversIteration;
    }

    void onReceive(const Asyncable* receiver, const Callback& f)
    {
        const std::thread::id thisThId = std::this_thread::get_id();
        ThreadData& thdata = threadData(thisThId);

        Receiver* r = new Receiver();
        r->receiver = const_cast<Asyncable*>(receiver);
        if (r->receiver) {
            r->receiver->async_connect(this);
        }
        r->callback = f;

        thdata.receivers.push_back(r);

        ++m_enabledReceiversCount;
    }

    bool disconnectReceiver(const Asyncable* a, const std::thread::id& connectThId)
    {
        assert(a);
        if (!a) {
            return false;
        }

        const std::thread::id thisThId = std::this_thread::get_id();
        assert(connectThId == thisThId);
        if (!(connectThId == thisThId)) {
            return false;
        }

        ThreadData& thdata = threadData(thisThId);
        if (thdata.receiversIteration) {
            return false;
        }

        // remove receiver
        auto it = findReceiver(thdata.receivers, a);
        if (it != thdata.receivers.end()) {
            Receiver* r = *it;
            if (r->enabled) {
                --m_enabledReceiversCount;
                assert(m_enabledReceiversCount.load() >= 0);
            }
            delete r;
            thdata.receivers.erase(it);
        }

        const_cast<Asyncable*>(a)->async_disconnect(this);
        return true;
    }

    void disableReceiver(const Asyncable* a, const std::thread::id& connectThId)
    {
        assert(a);
        if (!a) {
            return;
        }

        const std::thread::id thisThId = std::this_thread::get_id();
        assert(connectThId == thisThId);
        if (!(connectThId == thisThId)) {
            return;
        }

        ThreadData& thdata = threadData(thisThId);
        auto it = findReceiver(thdata.receivers, a);
        if (it != thdata.receivers.end()) {
            Receiver* r = *it;
            r->enabled = false;
            --m_enabledReceiversCount;
            assert(m_enabledReceiversCount.load() >= 0);
        }
    }

    bool isReceiverConnected(const Asyncable* a, const std::thread::id& connectThId) const
    {
        assert(a);
        if (!a) {
            return false;
        }

        const std::thread::id thisThId = std::this_thread::get_id();
        assert(connectThId == thisThId);
        if (!(connectThId == thisThId)) {
            return false;
        }

        ThreadData& thdata = threadData(thisThId);
        auto it = findReceiver(thdata.receivers, a);
        return it != thdata.receivers.end();
    }

    bool isConnected() const
    {
        int count = m_enabledReceiversCount.load();
        return count > 0;
    }
};
}
