/*
 * Copyright (c) 2018-2020, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/JsonArray.h>
#include <AK/JsonObject.h>
#include <AK/JsonValue.h>
#include <AK/StringView.h>

#ifndef KERNEL
#    include <AK/JsonParser.h>
#endif

namespace AK {

JsonValue::JsonValue(JsonValue const& other)
    : Variant(other)
{
    copy_from(other);
}

JsonValue& JsonValue::operator=(JsonValue const& other)
{
    if (this != &other) {
        clear();
        copy_from(other);
    }
    return *this;
}

void JsonValue::copy_from(JsonValue const& other)
{
    if (other.is_object())
        set(new JsonObject(other.as_object()));
    else if (other.is_array())
        set(new JsonArray(other.as_array()));
    else
        Variant::operator=(other);
}

JsonValue::JsonValue(long long value)
    : Variant((i64)value)
{
    static_assert(sizeof(long long unsigned) == 8);
}

JsonValue::JsonValue(long long unsigned value)
    : Variant((u64)value)
{
    static_assert(sizeof(long long unsigned) == 8);
}

JsonValue::JsonValue(JsonObject const& value)
    : Variant(new JsonObject(value))
{
}

JsonValue::JsonValue(JsonArray const& value)
    : Variant(new JsonArray(value))
{
}

JsonValue::JsonValue(JsonObject&& value)
    : Variant(new JsonObject(move(value)))
{
}

JsonValue::JsonValue(JsonArray&& value)
    : Variant(new JsonArray(move(value)))
{
}

void JsonValue::clear()
{
    if (has<JsonObject*>())
        delete get<JsonObject*>();
    if (has<JsonArray*>())
        delete get<JsonArray*>();

    set(Empty {});
}

#ifndef KERNEL
ErrorOr<JsonValue> JsonValue::from_string(StringView input)
{
    return JsonParser(input).parse();
}
#endif

DeprecatedString JsonValue::serialized() const
{
    StringBuilder builder;
    serialize(builder).release_value_but_fixme_should_propagate_errors();
    return builder.to_deprecated_string();
}

}
