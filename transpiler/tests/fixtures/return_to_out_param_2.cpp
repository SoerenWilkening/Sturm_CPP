// Q-A (sturm-5kgu.2) fixture 2: qbool `|` composition — exercises
// the `^=` rewrite with a non-trivial operator expression and two
// quantum-typed inputs. Covers the common oracle shape where a
// reversible routine decomposes a compound boolean via registered
// primitive ops (OR here; the primitive decomposition is Phase E's
// job — this fixture only cares that Q-A preserves the expression
// verbatim on the RHS of `^=`).
namespace sturm {
class qbool {
public:
    qbool() = default;
    qbool(bool) {}
    qbool& operator=(const qbool&) = default;
    qbool& operator^=(const qbool&) { return *this; }
};
} // namespace sturm

inline sturm::qbool operator|(const sturm::qbool&, const sturm::qbool&) {
    return sturm::qbool{};
}

using sturm::qbool;

[[clang::annotate("sturm::reversible")]]
qbool join(qbool a, qbool b) { return a | b; }
