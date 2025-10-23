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
#include <mutex>

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
            : queue(conf::QUEUE_CAPACITY) {}
    };

    struct ThreadData {
        std::thread::id threadId;
        std::vector<QueueData*> queues;

        inline void deleteAll(std::vector<Receiver*>& recs, Asyncable::IConnectable* conn) const
        {
            for (Receiver* r : recs) {
                if (r->receiver) {
                    r->receiver->async_disconnect(conn);
                }
                delete r;
            }
            recs.clear();
        }

        inline void clearAll(Asyncable::IConnectable* conn)
        {
            deleteAll(receivers, conn);
            deleteAll(pendingToAdd, conn);

            for (QueueData* qdata : queues) {
                delete qdata;
            }
            queues.clear();
        }

        inline bool addReceiver(const Asyncable* receiver, const Callback& f, Asyncable::Mode mode, Asyncable::IConnectable* conn)
        {
            bool needIncrement = false;
            Receiver* r = nullptr;
            if (receiver) {
                {
                    auto it = findByAsyncable(receivers, receiver);
                    if (it != receivers.end()) {
                        r = *it;
                    }
                }

                if (!r) {
                    // maybe it was just added and hasn't been moved to the main list yet
                    auto it = findByAsyncable(pendingToAdd, receiver);
                    if (it != pendingToAdd.end()) {
                        r = *it;
                    }
                }

                if (r) {
                    assert(mode != Asyncable::Mode::SetOnce && "callback is already setted");
                    if (mode == Asyncable::Mode::SetOnce) {
                        return needIncrement;
                    }
                }
            }

            if (r) {
                // replace
                r->callback = f;
            } else {
                // new
                r = new Receiver();
                r->receiver = const_cast<Asyncable*>(receiver);
                if (r->receiver) {
                    r->receiver->async_connect(conn);
                }
                r->callback = f;
                pendingToAdd.push_back(r);
                needIncrement = true;
            }
            return needIncrement;
        }

        inline bool removeReceiver(const Asyncable* a)
        {
            bool needDecrement = false;
            Receiver* r = nullptr;
            {
                auto it = findByAsyncable(receivers, a);
                if (it != receivers.end()) {
                    r = *it;
                }
            }

            if (!r) {
                // maybe it was just added and hasn't been moved to the main list yet
                auto it = findByAsyncable(pendingToAdd, a);
                if (it != pendingToAdd.end()) {
                    r = *it;
                }
            }

            if (!r) {
                return needDecrement;
            }

            if (r->enabled) {
                r->enabled = false;
                r->receiver = nullptr; // already disconnected
                needDecrement = true;
                pendingToRemove.push_back(r);
            }
            return needDecrement;
        }

        inline void receiversCall(const T&... args)
        {
            addPending();
            removePending();

            for (const Receiver* r : receivers) {
                if (r->enabled) {
                    r->callback(args ...);
                }
            }

            // during the execution of the callback,
            // they can remove and add new receivers,
            // we will apply them immediately.
            removePending();
            addPending();
        }

        inline void receiversCall(const CallMsg& m)
        {
            addPending();
            removePending();

            for (const Receiver* r : receivers) {
                if (r->enabled) {
                    m.func(r);
                }
            }

            // during the execution of the callback,
            // they can remove and add new receivers,
            // we will apply them immediately.
            removePending();
            addPending();
        }

    private:

        inline void addPending()
        {
            if (pendingToAdd.empty()) {
                return;
            }

            for (Receiver* r : pendingToAdd) {
                receivers.push_back(r);
            }
            pendingToAdd.clear();
        }

        inline void removePending()
        {
            if (pendingToRemove.empty()) {
                return;
            }

            for (Receiver* r : pendingToRemove) {
                auto it = std::find(receivers.begin(), receivers.end(), r);
                assert(it != receivers.end());
                if (it != receivers.end()) {
                    delete r;
                    receivers.erase(it);
                }
            }
            pendingToRemove.clear();
        }

        inline auto findByAsyncable(const std::vector<Receiver*>& recs, const Asyncable* a) const
        {
            auto it = std::find_if(recs.begin(), recs.end(), [a](const Receiver* r) {
                return a == r->receiver;
            });
            return it;
        }

        std::vector<Receiver*> receivers;
        std::vector<Receiver*> pendingToAdd;
        std::vector<Receiver*> pendingToRemove;
    };

    std::mutex m_thmutex;
    std::atomic<size_t> m_thcount = 0;
    std::vector<ThreadData*> m_threads;
    std::atomic<int> m_enabledReceiversCount = 0;

    ThreadData& threadData(const std::thread::id& thId)
    {
        size_t count = m_thcount.load();
        assert(count <= m_threads.size());
        for (size_t i = 0; i < count; ++i) {
            ThreadData* thdata = m_threads.at(i);
            assert(thdata);
            if (thdata && thdata->threadId == thId) {
                return *m_threads[i];
            }
        }

        // not found, we will create
        {
            // the `m_threads` collection itself doesn't change;
            // we don't lock it, we only lock a slot in this collection.
            // therefore, we can iterate over this collection
            // in other threads without a lock.
            // `m_thcount` limits the number of iterations (only filled slots).
            std::scoped_lock lock(m_thmutex);
            for (size_t i = 0; i < m_threads.size(); ++i) {
                ThreadData* thdata = m_threads.at(i);
                if (!thdata) {
                    // found an empty slot
                    thdata = new ThreadData();
                    thdata->threadId = thId;
                    m_threads[i] = thdata;
                    ++m_thcount;
                    return *m_threads[i];
                }
            }
        }

        assert(false && "thread pool exhausted");
        static ThreadData dummy;
        return dummy;
    }

    // IConnectable
    void disconnectAsyncable(Asyncable* a, const std::thread::id& connectThId) override
    {
        disconnect(a, connectThId);
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
                thdata.receiversCall(m);
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
                qdata->queue.port2()->onMessage(nullptr);
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
        sendThdata.receiversCall(args ...);

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

    ChannelImpl(size_t max_threads = conf::MAX_THREADS_PER_CHANNEL)
        : m_threads{std::min(max_threads, conf::MAX_THREADS), nullptr} {}

    ChannelImpl(const ChannelImpl&) = delete;
    ChannelImpl& operator=(const ChannelImpl&) = delete;

    ~ChannelImpl()
    {
        unregAllQueue();

        for (ThreadData* thdata : m_threads) {
            if (!thdata) {
                break;
            }

            thdata->clearAll(this);
            delete thdata;
        }
    }

    size_t maxThreads() const { return m_threads.size(); }

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

    void onReceive(const Asyncable* receiver, const Callback& f, Asyncable::Mode mode)
    {
        const std::thread::id thisThId = std::this_thread::get_id();
        ThreadData& thdata = threadData(thisThId);
        bool needIncrement = thdata.addReceiver(receiver, f, mode, this);
        if (needIncrement) {
            ++m_enabledReceiversCount;
        }
    }

    void disconnect(const Asyncable* a)
    {
        assert(a);
        if (a) {
            disconnect(a, a->async_connectThread(this));
        }
    }

    void disconnect(const Asyncable* a, std::thread::id connectThId)
    {
        assert(a);
        if (!a) {
            return;
        }

        const_cast<Asyncable*>(a)->async_disconnect(this);

        const std::thread::id thisThId = std::this_thread::get_id();

        auto removeReceiver = [this, a]() {
            const std::thread::id thisThId = std::this_thread::get_id();
            ThreadData& thdata = threadData(thisThId);
            bool needDecrement = thdata.removeReceiver(a);
            if (needDecrement) {
                --m_enabledReceiversCount;
                assert(m_enabledReceiversCount.load() >= 0);
            }
        };

        if (connectThId == thisThId) {
            removeReceiver();
        } else {
            // to unsubscribe, we need to execute the receiver
            // removing code in the thread to which we subscribed.
            // let's send a message to this thread with a remove function.
            // the message callback will be called for all receivers,
            // but we need it to be called only once,
            // so we'll call it only for the receiver we need.
            CallMsg msg;
            msg.func = [a, removeReceiver](const void* r) {
                if (reinterpret_cast<const Receiver*>(r)->receiver == a) {
                    removeReceiver();
                }
            };

            ThreadData& sendThdata = threadData(thisThId);
            sendToQueue(sendThdata, connectThId, msg);
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
