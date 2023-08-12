/*
 * Copyright (c) 2023, Karol Kosek <krkk@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "DedicatedWorkerGlobalScope.h"
#include <LibWeb/Bindings/DedicatedWorkerExposedInterfaces.h>
#include <LibWeb/Bindings/Intrinsics.h>

namespace Web::HTML {

DedicatedWorkerGlobalScope::DedicatedWorkerGlobalScope(JS::Realm& realm)
    : WorkerGlobalScope(realm)
{
    Bindings::add_dedicated_worker_exposed_interfaces(*this);
}

void DedicatedWorkerGlobalScope::initialize_web_interfaces()
{
    auto& realm = this->realm();

    Base::initialize_web_interfaces();
    set_prototype(&Bindings::ensure_web_prototype<Bindings::DedicatedWorkerGlobalScopePrototype>(realm, "DedicatedWorkerGlobalScope"));
}

}
