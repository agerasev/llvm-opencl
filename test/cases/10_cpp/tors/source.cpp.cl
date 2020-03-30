class Tors {
public:
    int &ctor;
    int &dtor;
    Tors(int &ctor, int &dtor) : ctor(ctor), dtor(dtor) {
        ctor += 1;
    }
    ~Tors() {
        dtor += 1;
    }
};

__kernel void kernel_main(__global int *a, __global int *b) {
    const int i = get_global_id(0);
    int ctor = 0, dtor = 0;
    for (int j = 0; j < i; ++j) {
        Tors(ctor, dtor);
    }
    a[i] = ctor;
    b[i] = dtor;
}
