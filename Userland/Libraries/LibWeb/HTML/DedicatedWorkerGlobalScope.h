/*
 * Copyright (c) 2023, Karol Kosek <krkk@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWeb/HTML/WorkerGlobalScope.h>

namespace Web::HTML {

class DedicatedWorkerGlobalScope : public WorkerGlobalScope {
    WEB_PLATFORM_OBJECT(DedicatedWorkerGlobalScope, WorkerGlobalScope);

public:
    // FIXME: undefined postMessage(any message, sequence<object> transfer);
    // FIXME: undefined postMessage(any message, optional StructuredSerializeOptions options = {});

    // FIXME: undefined close();

    // FIXME: attribute EventHandler onmessage;
    // FIXME: attribute EventHandler onmessageerror;

protected:
    explicit DedicatedWorkerGlobalScope(JS::Realm& realm);
    virtual void initialize_web_interfaces() override;
};

}
