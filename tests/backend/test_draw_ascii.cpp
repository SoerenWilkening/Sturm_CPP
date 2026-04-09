// test_draw_ascii.cpp — tests for sturm::draw_ascii.

#include "sturm/backend/draw_ascii.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/core/gate_kind.h"

#include <cassert>
#include <cstdint>
#include <string>

static sturm::GateRecord mk(sturm_gate_kind_t k,
                            uint32_t q0, uint32_t q1, uint32_t q2,
                            uint8_t n, double p = 0.0) {
    sturm::GateRecord r{};
    r.kind = k;
    r.qubits = {q0, q1, q2};
    r.n = n;
    r.param = p;
    return r;
}

static std::size_t count_lines(const std::string& s) {
    std::size_t c = 0;
    for (char ch : s) if (ch == '\n') ++c;
    return c;
}

// Empty IR: one row per qubit, just the headers.
static void test_empty() {
    sturm::GateIR ir;
    std::string s = sturm::draw_ascii(ir, 3);
    assert(count_lines(s) == 3);
    assert(s.find("q0: ") != std::string::npos);
    assert(s.find("q1: ") != std::string::npos);
    assert(s.find("q2: ") != std::string::npos);
    // No gate columns => no dashes.
    assert(s.find('-') == std::string::npos);
}

// Single-qubit H on q0, X on q1.
static void test_single_qubit_gates() {
    sturm::GateIR ir;
    ir.append(mk(STURM_GATE_H, 0, 0, 0, 1));
    ir.append(mk(STURM_GATE_X, 1, 0, 0, 1));
    std::string s = sturm::draw_ascii(ir, 2);
    assert(count_lines(s) == 2);
    // Row 0 should contain 'H', row 1 should contain 'X'.
    auto nl = s.find('\n');
    std::string row0 = s.substr(0, nl);
    std::string row1 = s.substr(nl + 1, s.find('\n', nl + 1) - (nl + 1));
    assert(row0.find('H') != std::string::npos);
    assert(row0.find('X') == std::string::npos);
    assert(row1.find('X') != std::string::npos);
    assert(row1.find('H') == std::string::npos);
}

// CX on (0,2) should place '*' on q0, 'X' on q2, '|' on q1.
static void test_cx_vertical_bar() {
    sturm::GateIR ir;
    ir.append(mk(STURM_GATE_CX, 0, 2, 0, 2));
    std::string s = sturm::draw_ascii(ir, 3);
    auto p0 = s.find('\n');
    auto p1 = s.find('\n', p0 + 1);
    std::string r0 = s.substr(0, p0);
    std::string r1 = s.substr(p0 + 1, p1 - p0 - 1);
    std::string r2 = s.substr(p1 + 1, s.find('\n', p1 + 1) - p1 - 1);
    assert(r0.find('*') != std::string::npos);
    assert(r1.find('|') != std::string::npos);
    assert(r2.find('X') != std::string::npos);
    // q1 must NOT have '*' or 'X'.
    assert(r1.find('*') == std::string::npos);
    assert(r1.find('X') == std::string::npos);
}

// CCX with controls (0,1) and target 2.
static void test_ccx() {
    sturm::GateIR ir;
    ir.append(mk(STURM_GATE_CCX, 0, 1, 2, 3));
    std::string s = sturm::draw_ascii(ir, 3);
    auto p0 = s.find('\n');
    auto p1 = s.find('\n', p0 + 1);
    std::string r0 = s.substr(0, p0);
    std::string r1 = s.substr(p0 + 1, p1 - p0 - 1);
    std::string r2 = s.substr(p1 + 1, s.find('\n', p1 + 1) - p1 - 1);
    assert(r0.find('*') != std::string::npos);
    assert(r1.find('*') != std::string::npos);
    assert(r2.find('X') != std::string::npos);
}

// SWAP: both involved qubits marked with 'x'.
static void test_swap() {
    sturm::GateIR ir;
    ir.append(mk(STURM_GATE_SWAP, 0, 1, 0, 2));
    std::string s = sturm::draw_ascii(ir, 2);
    // Two lowercase 'x' markers somewhere.
    std::size_t n = 0;
    for (char c : s) if (c == 'x') ++n;
    assert(n == 2);
}

// Row count and column growth with multiple gates.
static void test_column_growth() {
    sturm::GateIR ir;
    ir.append(mk(STURM_GATE_H,  0, 0, 0, 1));
    ir.append(mk(STURM_GATE_CX, 0, 1, 0, 2));
    ir.append(mk(STURM_GATE_T,  1, 0, 0, 1));
    std::string s = sturm::draw_ascii(ir, 2);
    assert(count_lines(s) == 2);
    auto nl = s.find('\n');
    std::string r0 = s.substr(0, nl);
    // Row 0 must contain H and '*' in that order.
    auto ph = r0.find('H');
    auto pc = r0.find('*');
    assert(ph != std::string::npos);
    assert(pc != std::string::npos);
    assert(ph < pc);
}

int main() {
    test_empty();
    test_single_qubit_gates();
    test_cx_vertical_bar();
    test_ccx();
    test_swap();
    test_column_growth();
    return 0;
}
