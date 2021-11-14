#include "type_name_pool.hpp"

namespace tiny {

const TypeName& TypeNamePool::primitive_type(PrimitiveType value) {
    for (const auto& type : m_types) {
        auto* t = std::get_if<PrimitiveType>(&type.m_value);

        if (!t) {
            continue;
        }

        if (*t == value) {
            return type;
        }
    }

    m_types.emplace_back(std::move(value));

    return m_types.back();
}

const TypeName& TypeNamePool::array(const TypeName& element) {
    for (const auto& type : m_types) {
        auto* t = std::get_if<TypeName::Array>(&type.m_value);

        if (!t) {
            continue;
        }

        if (t->element == &element) {
            return type;
        }
    }

    TypeName::Array array{&element};

    m_types.emplace_back(std::move(array));

    return m_types.back();
}

const TypeName& TypeNamePool::map(const TypeName& key, const TypeName& value) {
    for (const auto& type : m_types) {
        auto* t = std::get_if<TypeName::Map>(&type.m_value);

        if (!t) {
            continue;
        }

        if (t->key == &key && t->value == &value) {
            return type;
        }
    }

    TypeName::Map map{&key, &value};

    m_types.emplace_back(std::move(map));

    return m_types.back();
}

const TypeName& TypeNamePool::function(const TypeName& return_value, const TypeName** args_first,
                                       const TypeName** args_last) {
    for (const auto& type : m_types) {
        auto* t = std::get_if<TypeName::Function>(&type.m_value);

        if (!t) {
            continue;
        }

        if (t->return_value == &return_value &&
            std::distance(args_first, args_last) == t->args.size() &&
            std::equal(args_first, args_last, t->args.begin())) {
            return type;
        }
    }

    TypeName::Function function{&return_value, {args_first, args_last}};

    m_types.emplace_back(std::move(function));

    return m_types.back();
}

}  // namespace tiny
