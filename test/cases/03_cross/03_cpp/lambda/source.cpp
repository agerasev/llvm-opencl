extern "C" {
    int run(int, int);
}

template <typename F>
int call(F f, int x) {
    return f(x);
}

int run(int x, int y) {
    if (y % 2 == 0) {
        return call([y](int z) { return z + y; }, x);
    } else {
        return call([](int z) { return z*z; }, x);
    }
}
