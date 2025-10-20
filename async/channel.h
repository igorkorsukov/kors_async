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

#include <memory>

#include "asyncable.h"
#include "internal/channelimpl.h"

namespace kors::async {
template<typename ... T>
class Channel
{
public:

    using Callback = std::function<void (const T&...)>;

private:

    struct Data {
        ChannelImpl<T...> mainCh;
        std::unique_ptr<ChannelImpl<> > closeCh;
        std::unique_ptr<ChannelImpl<const Asyncable*> > disconnectCh;
    };

    std::shared_ptr<Data> m_data;

public:
    Channel()
        : m_data(std::make_shared<Data>())
    {
    }

    Channel(const Channel& ch)
        : m_data(ch.m_data)
    {
    }

    ~Channel() = default;

    Channel& operator=(const Channel& ch)
    {
        m_data = ch.m_data;
        return *this;
    }

    void send(const T&... args)
    {
        m_data->mainCh.send(SendMode::Auto, args ...);
    }

    void onReceive(const Asyncable* receiver, const Callback& f, Asyncable::Mode mode = Asyncable::Mode::SetOnce)
    {
        m_data->mainCh.onReceive(receiver, f, mode);
    }

    template<typename Func>
    void onReceive(const Asyncable* receiver, Func f, Asyncable::Mode mode = Asyncable::Mode::SetOnce)
    {
        Callback callback = [f](const T&... args) {
            f(args ...);
        };
        onReceive(receiver, callback);
    }

    void disconnect(const Asyncable* a)
    {
        const std::thread::id& connectThId = a->async_connectThread(&m_data->mainCh);
        bool ok = m_data->mainCh.disconnectReceiver(a, connectThId);
        if (!ok) {
            m_data->mainCh.disableReceiver(a, connectThId);
            if (!m_data->disconnectCh) {
                m_data->disconnectCh = std::make_unique<ChannelImpl<const Asyncable*> >();
                m_data->disconnectCh->onReceive(nullptr, [this](const Asyncable* a) {
                    disconnect(a);
                }, Asyncable::Mode::SetOnce);
            }

            m_data->disconnectCh->send(SendMode::Queue, a);
        }
    }

    void resetOnReceive(const Asyncable* a)
    {
        disconnect(a);
    }

    void close()
    {
        if (m_data->closeCh) {
            m_data->closeCh->send(SendMode::Auto);
        }
    }

    void onClose(const Asyncable* receiver, const std::function<void()>& f, Asyncable::Mode mode = Asyncable::Mode::SetOnce)
    {
        if (!m_data->closeCh) {
            m_data->closeCh = std::make_unique<ChannelImpl<> >();
        }
        m_data->closeCh->onReceive(receiver, f, mode);
    }

    template<typename Func>
    void onClose(const Asyncable* receiver, Func f, Asyncable::Mode mode = Asyncable::Mode::SetOnce)
    {
        std::function<void()> callback = [f]() {
            f();
        };
        onClose(receiver, callback, mode);
    }

    bool isConnected() const
    {
        return m_data->mainCh.isConnected();
    }

    uint64_t key() const { return reinterpret_cast<uint64_t>(m_data.get()); }
};
}
