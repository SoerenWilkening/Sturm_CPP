class SturmTranspile < Formula
  desc "C++ DSL transpiler for quantum-classical programming with compile-time uncompute"
  homepage "https://github.com/SoerenWilkening/Sturm_CPP"
  url "https://github.com/SoerenWilkening/Sturm_CPP/archive/refs/tags/v0.1.0.tar.gz"
  sha256 "PLACEHOLDER_FILLED_AT_PL6"
  license "AGPL-3.0-or-later"

  depends_on "cmake" => :build
  depends_on "llvm@17"

  def install
    llvm = Formula["llvm@17"]
    system "cmake", "-B", "build", "-S", ".",
           "-DLLVM_DIR=#{llvm.opt_lib}/cmake/llvm",
           "-DClang_DIR=#{llvm.opt_lib}/cmake/clang",
           "-DCMAKE_BUILD_TYPE=Release",
           *std_cmake_args
    system "cmake", "--build", "build"
    system "cmake", "--install", "build"
  end

  test do
    # Self-contained fixture: sturm/sturm.hpp alone does not expose qbool's
    # operator| (that lives in sturm/qtypes/qbool_ops.hpp and requires
    # STURM_BACKEND_ENABLED + sturm/control/when.hpp to wire up). The
    # transpiler silences diagnostics via IgnoringDiagConsumer, so a bad
    # header chain here silently emits a pass-through file and the
    # assert_match below fails with no useful stderr. Inlining the stub
    # — same shape as tests/smoke/minimal_or.cpp — makes the test a
    # deterministic black-box probe of the M7 matcher.
    (testpath/"src.cpp").write <<~CPP
      namespace sturm {
      class qbool {
      public:
          qbool() {}
          qbool(int) {}
          qbool(const qbool&) {}
      };
      inline qbool operator|(const qbool&, const qbool&) { return qbool{}; }
      }
      using sturm::qbool;
      void f() { qbool a{0}, b{0}; qbool tmp = a | b; (void)tmp; }
    CPP
    system bin/"sturm-transpile", "src.cpp", "--output-dir", testpath/"out",
           "--extra-arg=-I#{include}", "--extra-arg=-std=c++20"
    assert_match "uncompute_or", (testpath/"out/src.cpp").read
  end
end
