[[clang::annotate("sturm::reversible")]]
void __join_out(qbool a, qbool b, qbool& __join_out) {
    __join_out ^= a | b;
}
