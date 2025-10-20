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
#include <string>
#include <cassert>

#include "async.h"
#include "internal/channelimpl.h"

namespace kors::async {
enum class PromiseType {
    AsyncByPromise,
    AsyncByBody
};

template<typename ... T>
class Promise;
template<typename ... T>
class Promise
{
public:

    struct Result;

    struct Resolve
    {
        Resolve() = default;

        Resolve(Promise<T...> _p)
            : p(_p) {}

        [[nodiscard]] Result operator ()(const T& ... val) const
        {
            p.resolve(val ...);
            return {};
        }

    private:
        mutable Promise<T...> p;
    };

    struct Reject
    {
        Reject() = default;

        Reject(Promise<T...> _p)
            : p(_p) {}

        [[nodiscard]] Result operator ()(int code, const std::string& msg) const
        {
            p.reject(code, msg);
            return {};
        }

    private:
        mutable Promise<T...> p;
    };

    // Dummy struct, with the purpose to enforce that the body
    // of a Promise resolves OR rejects exactly once
    struct Result {
        static Result unchecked()
        {
            return {};
        }

    private:
        Result() = default;

        friend struct Resolve;
        friend struct Reject;
    };

    static Result dummy_result() { return Result::unchecked(); }

    using BodyResolveReject = std::function<Result (Resolve, Reject)>;
    using BodyResolve = std::function<Result (Resolve)>;

    Promise(BodyResolveReject body, PromiseType type)
        : m_data(std::make_shared<Data>())
    {
        m_data->has_reject = true;
        m_data->rejectCh = std::make_unique<ChannelImpl<int, std::string>>();

        Resolve res(*this);
        Reject rej(*this);

        switch (type) {
        case PromiseType::AsyncByPromise:
            Async::call(nullptr, [res, rej](BodyResolveReject body) mutable {
                body(res, rej);
            }, body);
            break;

        case PromiseType::AsyncByBody:
            body(res, rej);
            break;
        }
    }

    Promise(BodyResolveReject body, const std::thread::id& th = std::this_thread::get_id())
        : m_data(std::make_shared<Data>())
    {
        m_data->has_reject = true;
        m_data->rejectCh = std::make_unique<ChannelImpl<int, std::string>>();

        Resolve res(*this);
        Reject rej(*this);

        Async::call(nullptr, [res, rej](BodyResolveReject body) mutable {
            body(res, rej);
        }, body, th);
    }

    Promise(BodyResolve body, PromiseType type)
        : m_data(std::make_shared<Data>())
    {
        m_data->has_reject = false;

        Resolve res(*this);

        switch (type) {
        case PromiseType::AsyncByPromise:
            Async::call(nullptr, [res](BodyResolve body) mutable {
                body(res);
            }, body);
            break;

        case PromiseType::AsyncByBody:
            body(res);
            break;
        }
    }

    Promise(BodyResolve body, const std::thread::id& th = std::this_thread::get_id())
        : m_data(std::make_shared<Data>())
    {
        m_data->has_reject = false;

        Resolve res(*this);

        Async::call(nullptr, [res](BodyResolve body) mutable {
            body(res);
        }, body, th);
    }

    Promise(const Promise& p)
        : m_data(p.m_data) {}

    ~Promise() {}

    Promise& operator=(const Promise& p)
    {
        m_data = p.m_data;
        return *this;
    }

    Promise<T...>& onResolve(const Asyncable* receiver, const std::function<void(const T&...)>& callback)
    {
        m_data->resolveCh.onReceive(receiver, callback);
        return *this;
    }

    template<typename F>
    Promise<T...>& onResolve(const Asyncable* receiver, F f)
    {
        std::function<void(const T&...)> callback = [f](const T&... args) {
            f(args ...);
        };
        return onResolve(receiver, callback);
    }

    Promise<T...>& onReject(const Asyncable* receiver, std::function<void(int, const std::string&)> callback)
    {
        assert(m_data->has_reject && "This promise has no rejection");
        if (m_data->rejectCh) {
            m_data->rejectCh->onReceive(receiver, callback);
        }
        return *this;
    }

    template<typename F>
    Promise<T...>& onReject(const Asyncable* receiver, F f)
    {
        std::function<void(int, const std::string&)> callback = [f](int code, const std::string& msg) {
            f(code, msg);
        };
        return onReject(receiver, callback);
    }

private:
    Promise() = default;

    void resolve(const T& ... args)
    {
        m_data->resolveCh.send(SendMode::Auto, args ...);
    }

    void reject(int code, const std::string& msg)
    {
        assert(m_data->has_reject && "This promise has no rejection");
        if (m_data->rejectCh) {
            m_data->rejectCh->send(SendMode::Auto, code, msg);
        }
    }

    struct Data {
        ChannelImpl<T...> resolveCh;
        bool has_reject = false;
        std::unique_ptr<ChannelImpl<int, std::string>> rejectCh;
    };

    std::shared_ptr<Data> m_data;
};

template<typename ... T>
inline Promise<T...> make_promise(typename Promise<T...>::BodyResolveReject f, PromiseType type = PromiseType::AsyncByPromise)
{
    return Promise<T...>(f, type);
}

template<typename ... T>
inline Promise<T...> make_promise(typename Promise<T...>::BodyResolve f, PromiseType type = PromiseType::AsyncByPromise)
{
    return Promise<T...>(f, type);
}
}
