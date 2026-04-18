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
    (testpath/"src.cpp").write <<~CPP
      #include <sturm/sturm.hpp>
      void f() { qbool a{0}, b{0}; qbool tmp = a | b; (void)tmp; }
    CPP
    system "#{bin}/sturm-transpile", "src.cpp", "--output-dir", testpath/"out",
           "--extra-arg=-I#{include}", "--extra-arg=-std=c++20"
    assert_match "uncompute_or", (testpath/"out/src.cpp").read
  end
end
