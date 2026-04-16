// Phase I / PI-5 input for the sturm-transpile snapshot test.
//
// Exercises the self-adjoint pairing: the user registers
// `STURM_REGISTER_ADJOINT(my_swap, my_swap)` — the same function is
// its own adjoint. The PI-1 registry still records the forward
// FunctionDecl; the PI-2 matcher still emits a USER_ROUTINE op; the
// PI-4 uncompute pass still renders `invert(my_swap)(...)`. At
// runtime `invert(my_swap)` resolves through the PI-0 trait table
// back to `&::my_swap`, so the injected line is semantically a
// second call to the same function — the canonical shape for a
// self-inverse routine (e.g. SWAP, an in-place X-chain).
namespace sturm {
class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
};

namespace _detail {
template <typename FnPtr>
struct adjoint_of;
} // namespace _detail

template <typename R, typename... Args>
constexpr auto invert(R (*fn)(Args...)) noexcept {
    (void)fn;
    return _detail::adjoint_of<R (*)(Args...)>::value;
}
} // namespace sturm

#define STURM_REGISTER_ADJOINT(fn, adj)                                        \
    namespace sturm {                                                          \
    namespace _detail {                                                        \
    template <>                                                                \
    struct adjoint_of<decltype(&::fn)> {                                       \
        static constexpr auto value = &::adj;                                  \
    };                                                                         \
    }                                                                          \
    }

void my_swap(sturm::qbool& a, sturm::qbool& b) {
    (void)a; (void)b;
}

STURM_REGISTER_ADJOINT(my_swap, my_swap)

using sturm::qbool;

void demo() {
    qbool x;
    qbool y;
    my_swap(x, y);
}
