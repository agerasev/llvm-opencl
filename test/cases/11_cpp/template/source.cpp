extern "C" {
    int run_int(int);
    float run_float(float);
}

template <typename T>
T sqr(T x) {
    return x*x;
}

int run_int(int x) {
    return sqr(x);
}

float run_float(float x) {
    return sqr(x);
}
