#include <iostream>
#include "test.hh"

extern "C" {
void test_c_func() {
    std::cout << "test_output" << std::endl;
}
}
