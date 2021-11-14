#include <array>
#include <cassert>
#include <iostream>

#include "type_name_pool.hpp"

int main(int argc, char** argv) {
    using namespace tiny;

    TypeNamePool pool;

    auto& a = pool.primitive_type(PrimitiveType::INT);
    auto& b = pool.primitive_type(PrimitiveType::INT);

    auto& aa = pool.primitive_type(PrimitiveType::BOOL);

    assert(&a == &b);
    assert(&a != &aa);

    auto& c = pool.array(a);
    auto& d = pool.array(b);

    auto& cc = pool.array(aa);

    assert(&c == &d);
    assert(&c != &cc);

    auto& e = pool.map(a, b);
    auto& f = pool.map(a, b);

    auto& ee = pool.map(c, c);

    assert(&e == &f);
    assert(&e != &ee);

    std::array<const TypeName*, 2> args{{&a, &b}};

    auto& g = pool.function(a, args.data(), args.data() + args.size());
    auto& h = pool.function(a, args.data(), args.data() + args.size());

    auto& gg = pool.function(aa, args.data(), args.data() + args.size());

    assert(&g == &h);
    assert(&g != &gg);

    return 0;
}
