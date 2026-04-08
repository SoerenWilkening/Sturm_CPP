#pragma once
// counter_sink.hpp — CounterSink + thread-local current_sink (Step 2, spec §1.3)
// Default sink is a function-local static CounterSink (see default_counter_sink()).

#include "sturm/core/sink.hpp"

#include <string>
#include <string_view>
#include <unordered_map>

namespace sturm {

// ── CounterSink ───────────────────────────────────────────────────────────────
// Overrides every Sink method, incrementing an unordered_map<string,size_t>
// keyed by op name. Used as the default process-wide sink.
class CounterSink : public Sink {
public:
    // ── Arithmetic ────────────────────────────────────────────────────────
    void quantum_add(const std::vector<int>&, const std::vector<int>&, int) override { bump("quantum_add"); }
    void quantum_sub(const std::vector<int>&, const std::vector<int>&, int) override { bump("quantum_sub"); }
    void quantum_mul(const std::vector<int>&, const std::vector<int>&, int) override { bump("quantum_mul"); }
    void quantum_div(const std::vector<int>&, const std::vector<int>&, int) override { bump("quantum_div"); }
    void quantum_mod(const std::vector<int>&, const std::vector<int>&, int) override { bump("quantum_mod"); }
    void quantum_pow(const std::vector<int>&, const std::vector<int>&, int) override { bump("quantum_pow"); }

    // ── Bitwise ──────────────────────────────────────────────────────────
    void quantum_xor(const std::vector<int>&, const std::vector<int>&, int) override { bump("quantum_xor"); }
    void quantum_and(const std::vector<int>&, const std::vector<int>&, int) override { bump("quantum_and"); }
    void quantum_or (const std::vector<int>&, const std::vector<int>&, int) override { bump("quantum_or");  }
    void quantum_not(const std::vector<int>&, int)                          override { bump("quantum_not"); }

    // ── Shifts ────────────────────────────────────────────────────────────
    void quantum_shl(const std::vector<int>&, const std::vector<int>&, int) override { bump("quantum_shl"); }
    void quantum_shr(const std::vector<int>&, const std::vector<int>&, int) override { bump("quantum_shr"); }

    // ── Compare ───────────────────────────────────────────────────────────
    void quantum_eq (const std::vector<int>&, const std::vector<int>&, int, int) override { bump("quantum_eq");  }
    void quantum_neq(const std::vector<int>&, const std::vector<int>&, int, int) override { bump("quantum_neq"); }
    void quantum_lt (const std::vector<int>&, const std::vector<int>&, int, int) override { bump("quantum_lt");  }
    void quantum_le (const std::vector<int>&, const std::vector<int>&, int, int) override { bump("quantum_le");  }
    void quantum_gt (const std::vector<int>&, const std::vector<int>&, int, int) override { bump("quantum_gt");  }
    void quantum_ge (const std::vector<int>&, const std::vector<int>&, int, int) override { bump("quantum_ge");  }

    // ── Rotations / preparation ───────────────────────────────────────────
    void theta_add(int, double, int) override { bump("theta_add"); }
    void phi_add  (int, double, int) override { bump("phi_add");   }
    void prepare  (int, double)      override { bump("prepare");   }

    // ── Query ─────────────────────────────────────────────────────────────
    size_t count(std::string_view op) const {
        auto it = counts_.find(std::string(op));
        return it == counts_.end() ? 0u : it->second;
    }

    void reset() { counts_.clear(); }

private:
    std::unordered_map<std::string, size_t> counts_;

    void bump(const char* name) { ++counts_[name]; }
};

// ── Default process-wide sink ─────────────────────────────────────────────────
// Returns a function-local static CounterSink. This is the initial value of
// the thread-local g_sink in every thread.
inline CounterSink& default_counter_sink() {
    static CounterSink inst;
    return inst;
}

// ── Thread-local sink pointer ─────────────────────────────────────────────────
// Defined here (not in sink.hpp) because the definition requires CounterSink to
// exist so the initial value can be &default_counter_sink().
//
// inline thread_local so it can live in a header included by multiple TUs.
namespace detail {
inline thread_local Sink* g_sink = &default_counter_sink();
} // namespace detail

// ── Implementation of current_sink / set_current_sink ─────────────────────────
// Declared in sink.hpp, defined here.
inline Sink* current_sink() {
    return detail::g_sink;
}
inline void set_current_sink(Sink* s) {
    detail::g_sink = s;
}

} // namespace sturm
