/*
 * Copyright (c) 2022, Ben Abraham <ben.d.abraham@gmail.com>
 * Copyright (c) 2023, Karol Kosek <krkk@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "WorkerEnvironmentSettingsObject.h"
#include <LibWeb/Bindings/DedicatedWorkerExposedInterfaces.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/HTML/WorkerGlobalScope.h>

namespace Web::HTML {

// https://html.spec.whatwg.org/multipage/workers.html#set-up-a-worker-environment-settings-object
WebIDL::ExceptionOr<JS::NonnullGCPtr<WorkerEnvironmentSettingsObject>> WorkerEnvironmentSettingsObject::setup(NonnullOwnPtr<JS::ExecutionContext> execution_context, EnvironmentSettingsObject& outside_settings /* number unsafeWorkerCreationTime */)
{
    // 1. Let inherited origin be outside settings's origin.
    auto inherited_origin = outside_settings.origin();

    // 2. Let realm be the value of execution context's Realm component.
    auto realm = execution_context->realm;
    VERIFY(realm);

    // 3. Let worker global scope be realm's global object.
    auto& worker_global_scope = verify_cast<WorkerGlobalScope>(realm->global_object());

    // 4. Let settings object be a new environment settings object whose algorithms are defined as follows:
    // NOTE: See the functions defined for this class.
    // FIXME: Pass unsafeWorkerCreationTime.
    auto settings_object = realm->heap().allocate<WorkerEnvironmentSettingsObject>(*realm, move(execution_context), worker_global_scope, inherited_origin).release_allocated_value_but_fixme_should_propagate_errors();

    // 5. Set settings object's id to a new unique opaque string, creation URL to worker global scope's url, top-level creation URL to null, target browsing context to null, and active service worker to null.
    // FIXME: Set active service worker to null.
    settings_object->id = {};
    settings_object->creation_url = worker_global_scope.url();
    settings_object->top_level_creation_url = {};
    settings_object->target_browsing_context = nullptr;

    // 6. If worker global scope is a DedicatedWorkerGlobalScope object, then set settings object's top-level origin to outside settings's top-level origin.
    if (true) {
        settings_object->top_level_origin = outside_settings.top_level_origin;
    }
    // FIXME: 7. Otherwise, set settings object's top-level origin to an implementation-defined value.
    else {
    }

    // 8. Set realm's [[HostDefined]] field to settings object.
    // Non-Standard: We store the ESO next to the web intrinsics in a custom HostDefined object
    auto intrinsics = MUST_OR_THROW_OOM(realm->heap().allocate<Bindings::Intrinsics>(*realm, *realm));
    auto host_defined = make<Bindings::HostDefined>(settings_object, intrinsics);
    realm->set_host_defined(move(host_defined));

    // Non-Standard: We cannot fully initialize WorkerGlobalScope object until *after* the we set up
    //    the realm's [[HostDefined]] internal slot as the internal slot contains the web platform intrinsics
    TRY(worker_global_scope.initialize_web_interfaces({}));

    // 9. Return settings object.
    return settings_object;
}

// https://html.spec.whatwg.org/multipage/workers.html#script-settings-for-workers:api-base-url
AK::URL WorkerEnvironmentSettingsObject::api_base_url()
{
    // Return worker global scope's url.
    return m_worker_global_scope->url();
}

// https://html.spec.whatwg.org/multipage/workers.html#script-settings-for-workers:concept-settings-object-origin-2
Origin WorkerEnvironmentSettingsObject::origin()
{
    // Return a unique opaque origin if worker global scope's url's scheme is "data", and inherited origin otherwise.
    if (m_worker_global_scope->url().scheme() == "data")
        return Origin();
    return m_inherited_origin;
}

// https://html.spec.whatwg.org/multipage/workers.html#script-settings-for-workers:concept-settings-object-policy-container
PolicyContainer WorkerEnvironmentSettingsObject::policy_container()
{
    // Return worker global scope's cross-origin isolated capability.
    return m_worker_global_scope->policy_container();
}

// https://html.spec.whatwg.org/multipage/workers.html#script-settings-for-workers:concept-settings-object-cross-origin-isolated-capability
CanUseCrossOriginIsolatedAPIs WorkerEnvironmentSettingsObject::cross_origin_isolated_capability()
{
    // Return worker global scope's cross-origin isolated capability.
    using enum CanUseCrossOriginIsolatedAPIs;
    return m_worker_global_scope->cross_origin_isolated_capability() ? Yes : No;
}

void WorkerEnvironmentSettingsObject::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_worker_global_scope);
}

}
