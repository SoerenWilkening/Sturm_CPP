// Q-A (sturm-5kgu.2) fixture 4: empty-parameter-list forward. A
// reversible routine that takes no inputs and returns a freshly
// constructed qbool. Exercises the twin-assembly corner case where
// the parameter-joiner must NOT emit a leading comma before the
// synthesised out-parameter slot — a regression here would produce
// `void __nullary_out(, qbool& __nullary_out)`, which is syntactically
// ill-formed and would fail to compile on any downstream parse.
namespace sturm {
class qbool {
public:
    qbool() = default;
    qbool& operator^=(const qbool&) { return *this; }
};
} // namespace sturm

using sturm::qbool;

[[clang::annotate("sturm::reversible")]]
qbool nullary() { return qbool{}; }
