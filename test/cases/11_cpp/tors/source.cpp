extern "C" {
    void run(int, int *, int *);
}

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

void run(int i, int *ctor, int *dtor) {
    for (int j = 0; j < i; ++j) {
        Tors(*ctor, *dtor);
    }
}
