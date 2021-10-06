#pragma once

#include <memory>

namespace tiny {

template <typename Fn>
struct FunctionView {};

template <typename R, typename... Args>
struct FunctionView<R(Args...)> {
    template <typename Fn>
    FunctionView(Fn&& fn)
        : m_callable{std::addressof(fn)}, m_fn{[](void* callable, Args&&... args) {
              return (*static_cast<std::decay_t<Fn>*>(callable))(std::forward<Args>(args)...);
          }} {}

    template <typename... CallArgs>
    R operator()(CallArgs&&... args) const {
        if constexpr (std::is_same_v<R, void>) {
            m_fn(m_callable, std::forward<CallArgs>(args)...);
        } else {
            return m_fn(m_callable, std::forward<CallArgs>(args)...);
        }
    }

private:
    void* m_callable = nullptr;
    R (*m_fn)(void*, Args&&...) = nullptr;
};

}  // namespace tiny
