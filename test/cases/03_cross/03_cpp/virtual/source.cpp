extern "C" {
    int run(int, int);
}

class A {
public:
    virtual int f(int) = 0;
};

class B : public A {
public:
    int f(int x) override {
        return 2*x;
    }
};

class C : public A {
public:
    int f(int x) override {
        return x*x;
    }
};

int run(int x, int y) {
    if (y % 2 == 0) {
        return B().f(x);
    } else {
        return C().f(x);
    }
}
