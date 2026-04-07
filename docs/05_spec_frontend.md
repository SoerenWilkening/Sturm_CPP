# Technical Specification — Front-End Stage

Companion to `04_prd_frontend.md`. Defines concrete types, signatures, and behavior contracts. Implementation-ready.

## 1. Core Types

### 1.1 `QubitPool`

```cpp
namespace sturm {
class QubitPool {
public:
    static QubitPool& instance();
    int allocate();              // returns next free physical index
    void release(int idx);       // optional, no-op for now
private:
    std::atomic<int> next_{0};
};
}
```

Sentinel value `-1` means "no physical qubit assigned (bit is classical)".

### 1.2 `Sink`

```cpp
namespace sturm {
struct Sink {
    virtual ~Sink() = default;

    // arithmetic
    virtual void quantum_add(const std::vector<int>& a,
                             const std::vector<int>& b,
                             int control) = 0;
    virtual void quantum_sub(...);
    virtual void quantum_mul(...);
    virtual void quantum_div(...);
    virtual void quantum_mod(...);
    virtual void quantum_pow(...);

    // bitwise
    virtual void quantum_xor(...);
    virtual void quantum_and(...);
    virtual void quantum_or(...);
    virtual void quantum_not(...);

    // shifts
    virtual void quantum_shl(...);
    virtual void quantum_shr(...);

    // compare (target = result qbool qubit)
    virtual void quantum_eq(...);
    virtual void quantum_neq(...);
    virtual void quantum_lt(...);
    virtual void quantum_le(...);
    virtual void quantum_gt(...);
    virtual void quantum_ge(...);

    // rotations / preparation
    virtual void theta_add(int qubit, double delta, int control) = 0;
    virtual void phi_add  (int qubit, double delta, int control) = 0;
    virtual void prepare  (int qubit, double p) = 0;
};

Sink* current_sink();
void  set_current_sink(Sink*);
}
```

Each method takes the involved physical qubit indices (`std::vector<int>` per qint operand) and a `control` qubit index (`-1` if no active control).

### 1.3 `CounterSink` and `RecordingSink`

- `CounterSink`: holds `std::unordered_map<std::string, size_t>`, default sink at process startup.
- `RecordingSink`: holds `std::vector<Record>` where
  ```cpp
  struct Record {
      std::string op;
      std::vector<std::vector<int>> qubit_groups;
      std::vector<double> scalars;
      int control;
  };
  ```
  Used by tests via RAII installer:
  ```cpp
  struct ScopedSink {
      Sink* prev;
      ScopedSink(Sink* s) : prev(current_sink()) { set_current_sink(s); }
      ~ScopedSink() { set_current_sink(prev); }
  };
  ```

## 2. `qbool`

```cpp
namespace sturm {
class qbool {
public:
    qbool();                              // value=false, is_super=false, qubits[0]=-1
    qbool(bool v);                        // implicit
    explicit qbool(double p);             // preparation: is_super=true, allocates qubit, calls sink.prepare
    explicit operator bool() const;       // returns value (stub measurement)

    bool value;
    bool is_super;
    std::array<int,1> qubits;             // -1 = classical
};
}
```

- Conversions to/from `qint` per PRD §6.
- Allocation helper: `void ensure_qubit()` allocates `qubits[0]` from pool if `-1` and `is_super`.

## 3. `qint_t<Width>`

```cpp
namespace sturm {
template <std::size_t Width = 64>
class qint_t {
    static_assert(Width <= 64, "Width must fit in int64_t this stage");
public:
    using value_type = int64_t;
    using mask_type  = uint64_t;

    qint_t();                              // value=0, mask=0, qubits all -1
    qint_t(int64_t v);                     // implicit; mask=0
    explicit operator int64_t() const;     // stub measurement; returns value

    // conversions
    qint_t(const qbool&);                  // zero-extend bit 0
    explicit operator qbool() const;       // keep bit 0 only

    // assignment
    qint_t& operator=(int64_t);
    qint_t& operator=(const qint_t&);

    // arithmetic
    qint_t  operator+(const qint_t&) const;
    qint_t  operator-(const qint_t&) const;
    qint_t  operator*(const qint_t&) const;
    qint_t  operator/(const qint_t&) const;
    qint_t  operator%(const qint_t&) const;
    qint_t  operator-() const;
    qint_t& operator+=(const qint_t&);
    qint_t& operator-=(const qint_t&);
    qint_t& operator*=(const qint_t&);
    qint_t& operator/=(const qint_t&);
    qint_t& operator%=(const qint_t&);

    // bitwise
    qint_t  operator&(const qint_t&) const;
    qint_t  operator|(const qint_t&) const;
    qint_t  operator^(const qint_t&) const;
    qint_t  operator~() const;
    qint_t& operator&=(const qint_t&);
    qint_t& operator|=(const qint_t&);
    qint_t& operator^=(const qint_t&);

    // shifts
    qint_t  operator<<(int) const;
    qint_t  operator>>(int) const;
    qint_t& operator<<=(int);
    qint_t& operator>>=(int);

    // compare → qbool
    qbool operator==(const qint_t&) const;
    qbool operator!=(const qint_t&) const;
    qbool operator< (const qint_t&) const;
    qbool operator<=(const qint_t&) const;
    qbool operator> (const qint_t&) const;
    qbool operator>=(const qint_t&) const;

    // bit view
    qbool operator[](std::size_t i) const; // shares qubits[i]; modification semantics: lvalue proxy if needed

    // phase / amplitude proxies
    struct PhiProxy { qint_t& parent; void operator+=(double); void operator-=(double); };
    struct ThetaProxy { qint_t& parent; void operator+=(double); void operator-=(double); };
    PhiProxy   phi  () { return {*this}; }
    ThetaProxy theta() { return {*this}; }

    // data
    int64_t  value;
    uint64_t super_mask;
    std::array<int, Width> qubits;
};

using qint = qint_t<64>;

qint pow(const qint&, const qint&);
qint pow(const qint&, int64_t);
}
```

## 4. Operator Dispatch Algorithm

For every binary operator on `qint`:

```
1. compute new_mask = mask_transfer(op, a.mask, b.mask)
2. compute classical_result = classical_op(a.value, b.value)
   (always safe; classical bits are correct, superposed bits are don't-care)
3. if new_mask == 0:
       result.value = classical_result
       result.mask  = 0
       return
4. for each bit i where new_mask bit i == 1:
       if result.qubits[i] == -1:
           result.qubits[i] = QubitPool::instance().allocate()
   (also ensure a.qubits[i], b.qubits[i] allocated where their masks are set)
5. ctrl = current_control ? current_control->qubits[0] : -1
6. current_sink()->quantum_<op>(a.qubits_vec(), b.qubits_vec(), ctrl)
7. result.value = classical_result
   result.mask  = new_mask
8. return result
```

Unary ops drop the second operand. Comparison ops produce a `qbool` result and call the comparison sink method with the result qubit.

## 5. Mask Transfer Functions

| Op | Rule |
|---|---|
| `^` `&` `|` | `a.mask | b.mask` (per bit) |
| `~` | `a.mask` |
| `<<` `>>` | `a.mask` shifted by the amount, with vacated bits = 0 |
| `+` `-` `+=` `-=` | let `m = a.mask | b.mask`; let `i = ctz(m)` if `m != 0`; output mask = `m | ((~0ULL) << i)` (widen from lowest superposed bit upward to bit `Width-1`) |
| `*` `/` `%` `pow` | any superposed bit anywhere → output mask = all-ones over `Width` |
| compare | `is_super = (a.mask | b.mask) != 0` |

## 6. `WHEN` Macro

```cpp
namespace sturm::detail {
struct WhenGuard {
    qbool* prev_control;
    bool   run;
    WhenGuard(qbool& expr);   // sets thread-local current_control if expr.is_super, allocates qubit if needed
    ~WhenGuard();             // restores prev_control
    bool should_run() const { return run; }
};
template <class T>
WhenGuard make_when_guard(T&& expr) {
    static_assert(std::is_same_v<std::decay_t<T>, qbool>,
                  "WHEN requires a qbool expression");
    return WhenGuard(expr);
}
}
#define WHEN(expr) \
    if (auto _sturm_when_guard = ::sturm::detail::make_when_guard((expr)); \
        _sturm_when_guard.should_run())
```

Behavior:
- `expr.is_super == false && expr.value == false` → `run = false`, body skipped.
- `expr.is_super == false && expr.value == true`  → `run = true`, control unchanged.
- `expr.is_super == true` → `run = true`, allocates `expr.qubits[0]` if `-1`, sets thread-local `current_control = &expr`.
- Destructor restores `current_control = prev_control`.

Thread-local declared in `control/when.hpp`:
```cpp
namespace sturm::detail { inline thread_local qbool* current_control = nullptr; }
```

## 7. Conversion Implementations

- `qint(int64_t v)`: `value=v; mask=0; qubits.fill(-1);`
- `int64_t(qint)`: returns `value`. TODO marker.
- `qbool(bool v)`: `value=v; is_super=false; qubits[0]=-1;`
- `bool(qbool)`: returns `value`.
- `qint(qbool b)`: `value = b.value ? 1 : 0; mask = b.is_super ? 1 : 0; qubits.fill(-1); qubits[0] = b.qubits[0];`
- `qbool(qint a)` (explicit): `qbool out; out.value = a.value & 1; out.is_super = (a.mask & 1) != 0; out.qubits[0] = a.qubits[0]; return out;`

## 8. Tests (plain `assert`)

`test_qint_classical.cpp`
- For every operator: classical inputs produce expected `int64_t` result and `mask == 0`.
- Edge cases: overflow (defined as wrapping), shift by 0, shift by 63, division by nonzero.

`test_qint_superposed.cpp`
- Construct qint with one bit superposed (e.g. mask = 0x4). Run `+=`, assert mask widens upward.
- Run `^=`, assert mask = bitwise OR.
- Assert physical qubit allocated at superposed positions (`qubits[i] != -1`).

`test_qbool.cpp`
- `qbool(0.5)` → `is_super == true`, qubit allocated, sink received `prepare` call.
- bool round trip.

`test_conversions.cpp`
- `qint a = qbool(true);` → `a.value == 1, a.mask == 0`.
- `qint a = qbool(0.5);` → `a.value` don't-care, `a.mask == 1`.
- `qbool b = qbool(qint(0xFF));` → `b.value == true`, `b.is_super == false`.
- `int64_t x = static_cast<int64_t>(qint(42));` → `x == 42`.

`test_when.cpp`
- `WHEN(qbool(false)) { ... }` body not executed.
- `WHEN(qbool(true))  { ... }` body executed, `current_control == nullptr` inside.
- `WHEN(qbool(0.5))   { ... }` body executed, `current_control != nullptr` inside, restored after.

`test_sink_dispatch.cpp`
- Install `RecordingSink`. Run `qint += qint` with one superposed bit. Assert exactly one `quantum_add` record with expected qubit groups and `control == -1`.
- Same under `WHEN(qbool(0.5))` → assert `control == <flag qubit idx>`.
- Run `qint * qint` with one superposed bit → assert `quantum_mul` record AND output mask is all-ones.

## 9. CMake Skeleton

```
cmake_minimum_required(VERSION 3.20)
project(sturm CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(sturm INTERFACE)
target_include_directories(sturm INTERFACE include)

enable_testing()
add_subdirectory(tests)
```

`tests/CMakeLists.txt`:
```
foreach(t
  test_qint_classical
  test_qint_superposed
  test_qbool
  test_conversions
  test_when
  test_sink_dispatch)
  add_executable(${t} ${t}.cpp)
  target_link_libraries(${t} PRIVATE sturm)
  add_test(NAME ${t} COMMAND ${t})
endforeach()
```

## 10. Coding Conventions

- Header-only public surface in `include/sturm/`.
- `namespace sturm` for public, `namespace sturm::detail` for internals.
- No exceptions in hot paths; `assert` for invariants.
- No external deps.
- Every TODO marked `// TODO(backend):` so backend stage can grep.

## 11. Out-of-Spec for This Stage

- AND-fold of nested `WHEN`s.
- Ancilla cursor passed into ops.
- Adjoint registry.
- Real measurement.
- Kernel dispatch on virtual indices 0/1–64/65–128/ancilla per backend rules 1–7. The current sink methods take physical qubit indices directly; the virtual-index kernel layer is added when the backend lands.
