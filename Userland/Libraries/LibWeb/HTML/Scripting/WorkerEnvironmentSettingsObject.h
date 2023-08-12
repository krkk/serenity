/*
 * Copyright (c) 2022, Ben Abraham <ben.d.abraham@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibJS/Heap/Cell.h>
#include <LibWeb/HTML/Scripting/Environments.h>

namespace Web::HTML {

class WorkerEnvironmentSettingsObject final
    : public EnvironmentSettingsObject {
    JS_CELL(WorkerEnvironmentSettingsObject, EnvironmentSettingsObject);

public:
    // https://html.spec.whatwg.org/multipage/workers.html#set-up-a-worker-environment-settings-object
    static WebIDL::ExceptionOr<JS::NonnullGCPtr<WorkerEnvironmentSettingsObject>> setup(NonnullOwnPtr<JS::ExecutionContext>, EnvironmentSettingsObject& outside_settings);

    virtual ~WorkerEnvironmentSettingsObject() override = default;

    // ^EnvironmentSettingsObject
    JS::GCPtr<DOM::Document> responsible_document() override { return nullptr; }
    DeprecatedString api_url_character_encoding() override { return "UTF-8"; }
    AK::URL api_base_url() override;
    Origin origin() override;
    PolicyContainer policy_container() override;
    CanUseCrossOriginIsolatedAPIs cross_origin_isolated_capability() override;

private:
    WorkerEnvironmentSettingsObject(NonnullOwnPtr<JS::ExecutionContext> execution_context, JS::NonnullGCPtr<WorkerGlobalScope> worker_global_scope, Origin inherited_origin)
        : EnvironmentSettingsObject(move(execution_context))
        , m_worker_global_scope(worker_global_scope)
        , m_inherited_origin(move(inherited_origin))
    {
    }

    virtual void visit_edges(Cell::Visitor& visitor) override;

    JS::NonnullGCPtr<WorkerGlobalScope> m_worker_global_scope;
    HTML::Origin m_inherited_origin;
};

}
