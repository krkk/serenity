/*
 * Copyright (c) 2018-2020, Andreas Kling <kling@serenityos.org>
 * Copyright (c) 2022, Filiph Sandström <filiph.sandstrom@filfatstudios.com>
 * Copyright (c) 2022, Ali Mohammad Pur <mpfard@serenityos.org>
 * Copyright (c) 2023, Karol Kosek <krkk@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Concepts.h>
#include <AK/DeprecatedString.h>
#include <AK/String.h>
#include <LibGUI/Icon.h>
#include <LibGfx/Bitmap.h>
#include <LibGfx/Font/Font.h>
#include <LibGfx/SystemTheme.h>

namespace GUI {

namespace Detail {
struct Boolean {
    bool value;
};
using VariantUnderlyingType = AK::Variant<Empty, Boolean, float, i32, i64, u32, u64, String, Color, Gfx::IntPoint, Gfx::IntSize, Gfx::IntRect, Gfx::TextAlignment, Gfx::ColorRole, NonnullRefPtr<Gfx::Bitmap const>, NonnullRefPtr<Gfx::Font const>, GUI::Icon>;
}

class Variant : public Detail::VariantUnderlyingType {
public:
    using Detail::VariantUnderlyingType::Variant;
    using Detail::VariantUnderlyingType::operator=;

    Variant(JsonValue const&);
    Variant& operator=(JsonValue const&);
    Variant(bool v)
        : Variant(Detail::Boolean { v })
    {
    }
    Variant& operator=(bool v)
    {
        set(Detail::Boolean { v });
        return *this;
    }

    // FIXME: Remove this once we fully migrate to String
    template<typename T>
    Variant(T&& value)
    requires(IsConstructible<DeprecatedString, T>)
        : Variant(String::from_deprecated_string(value).release_value_but_fixme_should_propagate_errors())
    {
    }

    template<OneOfIgnoringCV<Gfx::Bitmap, Gfx::Font> T>
    Variant(T const& value)
        : Variant(NonnullRefPtr<RemoveCV<T> const>(value))
    {
    }
    template<OneOfIgnoringCV<Gfx::Bitmap, Gfx::Font> T>
    Variant& operator=(T&& value)
    {
        set(NonnullRefPtr<RemoveCV<T>>(forward<T>(value)));
        return *this;
    }

    ~Variant() = default;

    bool is_valid() const { return !has<Empty>(); }
    bool is_bool() const { return has<Detail::Boolean>(); }
    bool is_i32() const { return has<i32>(); }
    bool is_i64() const { return has<i64>(); }
    bool is_u32() const { return has<u32>(); }
    bool is_u64() const { return has<u64>(); }
    bool is_float() const { return has<float>(); }
    bool is_string() const { return has<String>(); }
    bool is_bitmap() const { return has<NonnullRefPtr<Gfx::Bitmap const>>(); }
    bool is_color() const { return has<Color>(); }
    bool is_icon() const { return has<GUI::Icon>(); }
    bool is_point() const { return has<Gfx::IntPoint>(); }
    bool is_size() const { return has<Gfx::IntSize>(); }
    bool is_rect() const { return has<Gfx::IntRect>(); }
    bool is_font() const { return has<NonnullRefPtr<Gfx::Font const>>(); }
    bool is_text_alignment() const { return has<Gfx::TextAlignment>(); }
    bool is_color_role() const { return has<Gfx::ColorRole>(); }

    bool as_bool() const { return get<Detail::Boolean>().value; }

    bool to_bool() const
    {
        return visit(
            [](Empty) { return false; },
            [](Detail::Boolean v) { return v.value; },
            [](Integral auto v) { return v != 0; },
            [](Gfx::IntPoint const& v) { return !v.is_zero(); },
            [](OneOf<Gfx::IntRect, Gfx::IntSize> auto const& v) { return !v.is_empty(); },
            [](Enum auto const&) { return true; },
            [](OneOf<float, String, Color, NonnullRefPtr<Gfx::Font const>, NonnullRefPtr<Gfx::Bitmap const>, GUI::Icon> auto const&) { return true; });
    }

    i32 as_i32() const { return get<i32>(); }
    i64 as_i64() const { return get<i64>(); }
    u32 as_u32() const { return get<u32>(); }
    u64 as_u64() const { return get<u64>(); }

    template<Integral T>
    T to_integer() const
    {
        return visit(
            [](Empty) -> T { return 0; },
            [](Integral auto v) { return static_cast<T>(v); },
            [](FloatingPoint auto v) { return (T)v; },
            [](Detail::Boolean v) -> T { return v.value ? 1 : 0; },
            [](String const& v) { return v.to_number<T>().value_or(0); },
            [](Enum auto const&) -> T { return 0; },
            [](OneOf<Gfx::IntPoint, Gfx::IntRect, Gfx::IntSize, Color, NonnullRefPtr<Gfx::Font const>, NonnullRefPtr<Gfx::Bitmap const>, GUI::Icon> auto const&) -> T { return 0; });
    }

    i32 to_i32() const { return to_integer<i32>(); }
    i64 to_i64() const { return to_integer<i64>(); }
    float as_float() const { return get<float>(); }

    float as_float_or(float fallback) const
    {
        if (auto const* p = get_pointer<float>())
            return *p;
        return fallback;
    }

    Gfx::IntPoint as_point() const { return get<Gfx::IntPoint>(); }
    Gfx::IntSize as_size() const { return get<Gfx::IntSize>(); }
    Gfx::IntRect as_rect() const { return get<Gfx::IntRect>(); }
    DeprecatedString as_string() const { return get<String>().to_deprecated_string(); }
    Gfx::Bitmap const& as_bitmap() const { return *get<NonnullRefPtr<Gfx::Bitmap const>>(); }
    GUI::Icon as_icon() const { return get<GUI::Icon>(); }
    Color as_color() const { return get<Color>(); }
    Gfx::Font const& as_font() const { return *get<NonnullRefPtr<Gfx::Font const>>(); }

    Gfx::TextAlignment to_text_alignment(Gfx::TextAlignment default_value) const
    {
        if (auto const* p = get_pointer<Gfx::TextAlignment>())
            return *p;
        return default_value;
    }

    Gfx::ColorRole to_color_role() const
    {
        if (auto const* p = get_pointer<Gfx::ColorRole>())
            return *p;
        return Gfx::ColorRole::NoRole;
    }

    Color to_color(Color default_value = {}) const
    {
        if (auto const* p = get_pointer<Color>())
            return *p;
        if (auto const* p = get_pointer<String>())
            return Color::from_string(*p).value_or(default_value);
        return default_value;
    }

    DeprecatedString to_deprecated_string() const
    {
        return MUST(to_string()).to_deprecated_string();
    }

    ErrorOr<String> to_string() const
    {
        return visit(
            [](Empty) { return "[null]"_string; },
            [](String v) -> ErrorOr<String> { return v; },
            [](Gfx::TextAlignment v) { return String::formatted("Gfx::TextAlignment::{}", Gfx::to_string(v)); },
            [](Gfx::ColorRole v) { return String::formatted("Gfx::ColorRole::{}", Gfx::to_string(v)); },
            [](NonnullRefPtr<Gfx::Font const> const& font) { return String::formatted("[Font: {}]", font->name()); },
            [](NonnullRefPtr<Gfx::Bitmap const> const&) { return "[Gfx::Bitmap]"_string; },
            [](GUI::Icon const&) { return "[GUI::Icon]"_string; },
            [](Detail::Boolean v) { return String::formatted("{}", v.value); },
            [](auto const& v) { return String::formatted("{}", v); });
    }

    bool operator==(Variant const&) const;
    bool operator<(Variant const&) const;
};

}
