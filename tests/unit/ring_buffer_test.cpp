#include <cassert>
#include <vector>
#include "core/ring_buffer.hpp"

int main() {
    RingBufferI16 rb(8);
    std::vector<int16_t> in{1,2,3,4,5};
    size_t w = rb.push(in.data(), in.size());
    assert(w == in.size());
    std::vector<int16_t> out(5);
    size_t r = rb.pop(out.data(), out.size());
    assert(r == out.size());
    for (size_t i = 0; i < out.size(); ++i) assert(out[i] == in[i]);
    return 0;
}
