// entry_point_gtest_wrapping.cpp -- sturm-0tcv fixture input.
//
// PRD §5.4 (post-sturm-0tcv): GoogleTest TEST_F bodies are
// CXXMethodDecls on a generated fixture class. The matcher refuses
// CXXMethodDecl subjects per the §5.4 out-of-scope envelope, so the
// recommended idiom is:
//
//   1. Write the test body inside a free function annotated with
//      `[[sturm::entry_point]]` (the function below — TestFooBody).
//   2. Call that free function from the TEST_F method body (the
//      Fixture::TestBody method below mimics the generated TEST_F
//      shape).
//
// Expected rewrite shape (see .expected.cpp):
//   * `TestFooBody()` is auto-wrapped with the void-IIFE shape.
//   * The method `Fixture::TestBody()` is left untouched — the
//     matcher rejects CXXMethodDecl subjects.

#define STURM_UMBRELLA_INCLUDED 1

[[clang::annotate("sturm::entry_point")]]
void TestFooBody() {
    sturm_backend_context_t* __sturm_ctx = sturm_backend_create(STURM_MODE_DEFAULT);
    sturm_set_thread_context(__sturm_ctx);
    ([&]() -> void {
    int x = 0;
    (void)x;
})();
    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(__sturm_ctx);
}

class Fixture {
public:
    void TestBody() {
        TestFooBody();
    }
};
