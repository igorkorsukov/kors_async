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

#include "internal/channelimpl.h"

namespace kors::async {
class Async
{
private:
    using Call = std::function<void ()>;
    ChannelImpl<ChannelImpl<Call>*> m_deleter;

    Async()
    {
        m_deleter.onReceive(nullptr, [](ChannelImpl<Call>* ch) {
            delete ch;
        });
    }

    void deleteLater(ChannelImpl<Call>* ch)
    {
        m_deleter.send(SendMode::Queue, ch);
    }

    void callQueue(const Asyncable* caller, const Call& func, const std::thread::id& th)
    {
        ChannelImpl<Call>* ch = new ChannelImpl<Call>();
        ch->onReceive(caller, [ch](const Call& func) {
            func();
            inctance()->deleteLater(ch);
        }, th);
        ch->send(SendMode::Queue, func);
    }

public:

    static Async* inctance()
    {
        static Async a;
        return &a;
    }

    static void call(const Asyncable* caller, const Call& func, const std::thread::id& th = std::this_thread::get_id())
    {
        inctance()->callQueue(caller, func, th);
    }

    template<typename F>
    static void call(const Asyncable* caller, F f, const std::thread::id& th = std::this_thread::get_id())
    {
        Call c = [f]() { f(); };
        Async::call(caller, c, th);
    }

    template<typename F, typename Arg1>
    static void call(const Asyncable* caller, F f, Arg1 a1, const std::thread::id& th = std::this_thread::get_id())
    {
        Call c = [f, a1]() { f(a1); };
        Async::call(caller, c, th);
    }
};
}
