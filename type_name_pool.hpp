#pragma once

#include <deque>

#include "type_name.hpp"

namespace tiny {

struct TypeNamePool {
    const TypeName& primitive_type(PrimitiveType value);
    const TypeName& array(const TypeName& element);
    const TypeName& map(const TypeName& key, const TypeName& value);
    const TypeName& function(const TypeName& return_value, const TypeName** args_first,
                             const TypeName** args_last);

private:
    // We use a deque so that insertion does not invalidate existing pointers.
    std::deque<TypeName> m_types;
};

}  // namespace tiny
