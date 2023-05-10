/*
 * Copyright (c) 2018-2020, Andreas Kling <kling@serenityos.org>
 * Copyright (c) 2023, Karol Kosek <krkk@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Forward.h>
#include <AK/StringBuilder.h>
#include <AK/Variant.h>

#ifndef KERNEL
#    include <AK/DeprecatedString.h>
#endif

namespace AK {

namespace Detail {
struct Boolean {
    bool value;
};

#ifndef KERNEL
using JsonValueUnderlyingType = AK::Variant<Empty, i32, u32, i64, u64, double, Boolean, DeprecatedString, JsonArray*, JsonObject*>;
#else
using JsonValueUnderlyingType = AK::Variant<Empty, i32, u32, i64, u64, Boolean, JsonArray*, JsonObject*>;
#endif
}

class JsonValue : public Detail::JsonValueUnderlyingType {
public:
    using Detail::JsonValueUnderlyingType::Variant;

    static ErrorOr<JsonValue> from_string(StringView);

    JsonValue() = default;
    ~JsonValue() { clear(); }

    JsonValue(JsonValue const&);
    JsonValue& operator=(JsonValue const&);

    JsonValue(long long);
    JsonValue(long long unsigned);

#ifndef KERNEL
    template<typename T>
    JsonValue(T&& value)
    requires(IsConstructible<DeprecatedString, T>)
        : JsonValue(DeprecatedString(forward<T>(value)))
    {
    }
#endif

    template<typename T>
    requires(SameAs<RemoveCVReference<T>, bool>)
    JsonValue(T value)
        : Detail::JsonValueUnderlyingType(Detail::Boolean { value })
    {
    }

    JsonValue(JsonArray const&);
    JsonValue(JsonObject const&);

    JsonValue(JsonArray&&);
    JsonValue(JsonObject&&);

    // FIXME: Implement these
    JsonValue& operator=(JsonArray&&) = delete;
    JsonValue& operator=(JsonObject&&) = delete;

    template<typename Builder>
    typename Builder::OutputType serialized() const;
    template<typename Builder>
    void serialize(Builder&) const;

#ifndef KERNEL
    DeprecatedString as_string_or(DeprecatedString const& alternative) const
    {
        if (is_string())
            return as_string();
        return alternative;
    }

    DeprecatedString to_deprecated_string() const
    {
        if (is_string())
            return as_string();
        return serialized<StringBuilder>();
    }
#endif

    int to_int(int default_value = 0) const
    {
        return to_i32(default_value);
    }
    i32 to_i32(i32 default_value = 0) const { return to_number<i32>(default_value); }
    i64 to_i64(i64 default_value = 0) const { return to_number<i64>(default_value); }

    unsigned to_uint(unsigned default_value = 0) const { return to_u32(default_value); }
    u32 to_u32(u32 default_value = 0) const { return to_number<u32>(default_value); }
    u64 to_u64(u64 default_value = 0) const { return to_number<u64>(default_value); }
#if !defined(KERNEL)
    float to_float(float default_value = 0) const
    {
        return to_number<float>(default_value);
    }
    double to_double(double default_value = 0) const { return to_number<double>(default_value); }
#endif

    FlatPtr to_addr(FlatPtr default_value = 0) const
    {
        return to_number<FlatPtr>(default_value);
    }

    bool to_bool(bool default_value = false) const
    {
        if (!is_bool())
            return default_value;
        return as_bool();
    }

    i32 as_i32() const { return get<i32>(); }
    u32 as_u32() const { return get<u32>(); }
    i64 as_i64() const { return get<i64>(); }
    u64 as_u64() const { return get<u64>(); }
    bool as_bool() const { return get<Detail::Boolean>().value; }

#ifndef KERNEL
    DeprecatedString as_string() const
    {
        return get<DeprecatedString>();
    }
#endif

    JsonObject& as_object()
    {
        return *get<JsonObject*>();
    }
    JsonObject const& as_object() const { return *get<JsonObject*>(); }
    JsonArray& as_array() { return *get<JsonArray*>(); }
    JsonArray const& as_array() const { return *get<JsonArray*>(); }

#if !defined(KERNEL)
    double as_double() const
    {
        return get<double>();
    }
#endif
    bool is_null() const
    {
        return has<Empty>();
    }
    bool is_bool() const { return has<Detail::Boolean>(); }
#ifndef KERNEL
    bool is_string() const
    {
        return has<DeprecatedString>();
    }
#endif
    bool is_i32() const
    {
        return has<i32>();
    }
    bool is_u32() const { return has<u32>(); }
    bool is_i64() const { return has<i64>(); }
    bool is_u64() const { return has<u64>(); }

#if !defined(KERNEL)
    bool is_double() const
    {
        return has<double>();
    }
#endif
    bool is_array() const
    {
        return has<JsonArray*>();
    }
    bool is_object() const { return has<JsonObject*>(); }

    bool is_number() const
    {
#ifndef KERNEL
        return has<i32>() || has<u32>() || has<i64>() || has<u64>() || has<double>();
#else
        return has<i32>() || has<u32>() || has<i64>() || has<u64>();
#endif
    }

    template<typename T>
    T to_number(T default_value = 0) const
    {
        return visit(
#ifndef KERNEL
            [](double v) -> T { return v; },
#endif
            [](Integral auto v) -> T { return v; },
            [default_value](auto) { return default_value; });
    }

    template<Integral T>
    bool is_integer() const
    {
        if (has<i32>())
            return is_within_range<T>(get<i32>());
        if (has<u32>())
            return is_within_range<T>(get<u32>());
        if (has<i64>())
            return is_within_range<T>(get<i64>());
        if (has<u64>())
            return is_within_range<T>(get<u64>());
        return false;
    }

    template<Integral T>
    T as_integer() const
    {
        VERIFY(is_integer<T>());

        if (has<i32>())
            return static_cast<T>(get<i32>());
        if (has<u32>())
            return static_cast<T>(get<u32>());
        if (has<i64>())
            return static_cast<T>(get<i64>());
        if (has<u64>())
            return static_cast<T>(get<u64>());
        VERIFY_NOT_REACHED();
    }

private:
    void clear();
    void copy_from(JsonValue const&);
};

#ifndef KERNEL
template<>
struct Formatter<JsonValue> : Formatter<StringView> {
    ErrorOr<void> format(FormatBuilder& builder, JsonValue const& value)
    {
        return Formatter<StringView>::format(builder, value.to_deprecated_string());
    }
};
#endif

}

#if USING_AK_GLOBALLY
using AK::JsonValue;
#endif
