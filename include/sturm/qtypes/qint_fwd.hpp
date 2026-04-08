#pragma once
// qint_fwd.hpp — Forward declaration of qint_t<Width> and the qint alias.
// Step 6 Module A, spec §3, Implementation Plan §6.
//
// Include this header to break include cycles — it does not pull in the full
// class definition. Any code that needs the full interface must include qint.hpp.

#include <cstddef>  // std::size_t
#include <cstdint>  // int64_t, uint64_t

namespace sturm {

// Forward-declare the template class.
template <std::size_t Width = 64>
class qint_t;

// Canonical alias for 64-bit quantum integer.
using qint = qint_t<64>;

} // namespace sturm
