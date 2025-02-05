/*
 * Copyright (c) 2023, Tim Flynn <trflynn89@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibJS/Forward.h>
#include <LibWeb/Bindings/PlatformObject.h>

namespace Web::Internals {

class Inspector final : public Bindings::PlatformObject {
    WEB_PLATFORM_OBJECT(Inspector, Bindings::PlatformObject);
    JS_DECLARE_ALLOCATOR(Inspector);

public:
    virtual ~Inspector() override;

    void inspector_loaded();
    void inspect_dom_node(i32 node_id, Optional<i32> const& pseudo_element);

    void set_dom_node_text(i32 node_id, String const& text);
    void set_dom_node_tag(i32 node_id, String const& tag);
    void replace_dom_node_attribute(i32 node_id, String const& name, JS::NonnullGCPtr<DOM::NamedNodeMap> replacement_attributes);

    void execute_console_script(String const& script);

private:
    explicit Inspector(JS::Realm&);

    virtual void initialize(JS::Realm&) override;
};

}
