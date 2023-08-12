/*
 * Copyright (c) 2022, Ben Abraham <ben.d.abraham@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "LibWeb/Bindings/DedicatedWorkerExposedInterfaces.h"
#include <AK/Debug.h>
#include <LibJS/Runtime/ConsoleObject.h>
#include <LibJS/Runtime/Realm.h>
#include <LibWeb/Bindings/MainThreadVM.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/Fetch/Infrastructure/FetchAlgorithms.h>
#include <LibWeb/HTML/Scripting/Environments.h>
#include <LibWeb/HTML/Scripting/Fetching.h>
#include <LibWeb/HTML/Worker.h>
#include <LibWeb/HTML/WorkerDebugConsoleClient.h>
#include <LibWeb/HTML/WorkerGlobalScope.h>
#include <LibWeb/WebIDL/ExceptionOr.h>

namespace Web::HTML {

// https://html.spec.whatwg.org/multipage/workers.html#dedicated-workers-and-the-worker-interface
Worker::Worker(String const& script_url, WorkerOptions const options, DOM::Document& document)
    : DOM::EventTarget(document.realm())
    , m_script_url(script_url)
    , m_options(options)
    , m_document(&document)
    , m_custom_data()
    , m_worker_vm(JS::VM::create(adopt_own(m_custom_data)).release_value_but_fixme_should_propagate_errors())
    , m_implicit_port(MessagePort::create(document.realm()).release_value_but_fixme_should_propagate_errors())
{
    m_custom_data.event_loop.set_vm(m_worker_vm);
}

void Worker::initialize(JS::Realm& realm)
{
    Base::initialize(realm);
    set_prototype(&Bindings::ensure_web_prototype<Bindings::WorkerPrototype>(realm, "Worker"));
}

void Worker::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_document);
    visitor.visit(m_inner_settings);
    visitor.visit(m_implicit_port);
    visitor.visit(m_outside_port);

    // These are in a separate VM and shouldn't be visited
    visitor.ignore(m_worker_realm);
    visitor.ignore(m_worker_scope);
}

// https://html.spec.whatwg.org/multipage/workers.html#dom-worker
WebIDL::ExceptionOr<JS::NonnullGCPtr<Worker>> Worker::create(String const& script_url, WorkerOptions const options, DOM::Document& document)
{
    dbgln_if(WEB_WORKER_DEBUG, "WebWorker: Creating worker with script_url = {}", script_url);

    // Returns a new Worker object. scriptURL will be fetched and executed in the background,
    // creating a new global environment for which worker represents the communication channel.
    // options can be used to define the name of that global environment via the name option,
    // primarily for debugging purposes. It can also ensure this new global environment supports
    // JavaScript modules (specify type: "module"), and if that is specified, can also be used
    // to specify how scriptURL is fetched through the credentials option.

    // FIXME: 1. The user agent may throw a "SecurityError" DOMException if the request violates
    // a policy decision (e.g. if the user agent is configured to not allow the page to start dedicated workers).
    // Technically not a fixme if our policy is not to throw errors :^)

    // 2. Let outside settings be the current settings object.
    auto& outside_settings = current_settings_object();

    // 3. Parse the scriptURL argument relative to outside settings.
    auto url = document.parse_url(script_url.to_deprecated_string());

    // 4. If this fails, throw a "SyntaxError" DOMException.
    if (!url.is_valid()) {
        dbgln_if(WEB_WORKER_DEBUG, "WebWorker: Invalid URL loaded '{}'.", script_url);
        return WebIDL::SyntaxError::create(document.realm(), "url is not valid");
    }

    // 5. Let worker URL be the resulting URL record.

    // 6. Let worker be a new Worker object.
    auto worker = MUST_OR_THROW_OOM(document.heap().allocate<Worker>(document.realm(), script_url, options, document));

    // 7. Let outside port be a new MessagePort in outside settings's Realm.
    auto outside_port = TRY(MessagePort::create(outside_settings.realm()));

    // 8. Associate the outside port with worker
    worker->m_outside_port = outside_port;

    // 9. Run this step in parallel:
    //    1. Run a worker given worker, worker URL, outside settings, outside port, and options.
    worker->run_a_worker(url, outside_settings, *outside_port, options);

    // 10. Return worker
    return worker;
}

// https://html.spec.whatwg.org/multipage/workers.html#run-a-worker
void Worker::run_a_worker(AK::URL& url, EnvironmentSettingsObject& outside_settings, MessagePort& outside_port, WorkerOptions const& options)
{
    // 1. Let is shared be true if worker is a SharedWorker object, and false otherwise.
    // FIXME: SharedWorker support
    auto is_shared = false;

    // 2. Let owner be the relevant owner to add given outside settings.
    // FIXME: Support WorkerGlobalScope options
    if (!is<HTML::WindowEnvironmentSettingsObject>(outside_settings))
        TODO();

    // 3. Let parent worker global scope be null.
    // 4. If owner is a WorkerGlobalScope object (i.e., we are creating a nested dedicated worker),
    //    then set parent worker global scope to owner.
    // FIXME: Support for nested workers.

    // 5. Let unsafeWorkerCreationTime be the unsafe shared current time.

    // 6. Let agent be the result of obtaining a dedicated/shared worker agent given outside settings
    // and is shared. Run the rest of these steps in that agent.
    // NOTE: This is effectively the worker's vm

    // 7. Let realm execution context be the result of creating a new JavaScript realm given agent and the following customizations:
    auto realm_execution_context = Bindings::create_a_new_javascript_realm(
        *m_worker_vm,
        [&](JS::Realm& realm) -> JS::Object* {
            // For the global object, if is shared is true, create a new SharedWorkerGlobalScope object.
            // Otherwise, create a new DedicatedWorkerGlobalScope object.
            // FIXME: Proper support for both SharedWorkerGlobalScope and DedicatedWorkerGlobalScope
            if (is_shared)
                TODO();
            auto foo = realm.heap().allocate<WorkerGlobalScope>(realm, realm).release_allocated_value_but_fixme_should_propagate_errors();
            // FIXME: Shared workers should use the shared worker method
            Bindings::add_dedicated_worker_exposed_interfaces(foo);
            return foo;
        },
        nullptr);

    auto& console_object = *realm_execution_context->realm->intrinsics().console_object();
    m_worker_realm = realm_execution_context->realm;

    m_console = adopt_ref(*new WorkerDebugConsoleClient(console_object.console()));
    console_object.console().set_client(*m_console);

    // 8. Let worker global scope be the global object of realm execution context's Realm component.
    m_worker_scope = verify_cast<WorkerGlobalScope>(realm_execution_context->realm->global_object());

    // 9. Set up a worker environment settings object with realm execution context,
    //    outside settings, and unsafeWorkerCreationTime, and let inside settings be the result.
    m_inner_settings = WorkerEnvironmentSettingsObject::setup(move(realm_execution_context), outside_settings).release_value_but_fixme_should_propagate_errors();

    // 10. Set worker global scope's name to the value of options's name member.
    // FIXME: name property requires the SharedWorkerGlobalScope or DedicatedWorkerGlobalScope child class to be used

    // 11. Append owner to worker global scope's owner set.
    // FIXME: support for 'owner' set on WorkerGlobalScope

    // 12. If is shared is true, then:
    if (is_shared) {
        // FIXME: Shared worker support
        // 1. Set worker global scope's constructor origin to outside settings's origin.
        // 2. Set worker global scope's constructor url to url.
        // 3. Set worker global scope's type to the value of options's type member.
        // 4. Set worker global scope's credentials to the value of options's credentials member.
    }

    // 13. Let destination be "sharedworker" if is shared is true, and "worker" otherwise.
    auto destination = is_shared ? Fetch::Infrastructure::Request::Destination::SharedWorker : Fetch::Infrastructure::Request::Destination::Worker;

    // 14. Obtain script by switching on the value of options's type member:
    //     In both cases, let performFetch be the following perform the fetch hook given request, isTopLevel and processCustomFetchResponse:
    //     In both cases, let onComplete given script be the following steps:
    // FIXME: Perform steps with performFetch.
    OnFetchScriptComplete on_complete = [this, is_shared, &outside_port](JS::GCPtr<Script> script) {
        // 1. If script is null or if script's error to rethrow is non-null, then:
        if (!script || !script->error_to_rethrow().is_null()) {
            TODO();
        }

        // 2. Associate worker with worker global scope.

        // 3. Let inside port be a new MessagePort object in inside settings's realm.
        auto inside_port = MessagePort::create(m_inner_settings->realm()).release_value_but_fixme_should_propagate_errors();

        // 4. Associate inside port with worker global scope.

        // FIXME: Global scope association

        // 5. Entangle outside port and inside port.
        outside_port.entangle_with(*inside_port);

        // 6. Create a new WorkerLocation object and associate it with worker global scope.
        auto location = m_worker_scope->heap().allocate<WorkerLocation>(m_worker_scope->realm(), *m_worker_scope).release_allocated_value_but_fixme_should_propagate_errors();
        m_worker_scope->set_location(location);

        // 7. Closing orphan workers: Start monitoring the worker such that no sooner than it
        //    stops being a protected worker, and no later than it stops being a permissible worker,
        //    worker global scope's closing flag is set to true.
        // FIXME: Worker monitoring and cleanup

        // 8. Suspending workers: Start monitoring the worker, such that whenever worker global scope's
        //    closing flag is false and the worker is a suspendable worker, the user agent suspends
        //    execution of script in that worker until such time as either the closing flag switches to
        //    true or the worker stops being a suspendable worker
        // FIXME: Worker suspending

        // 9. Set inside settings's execution ready flag.
        m_inner_settings->execution_ready = true;

        // 10. If script is a classic script, then run the classic script script.
        //     Otherwise, it is a module script; run the module script script.
        if (is<ClassicScript>(*script)) {
            auto result = verify_cast<ClassicScript>(*script).run();
        } else if (is<JavaScriptModuleScript>(*script)) {
            verify_cast<JavaScriptModuleScript>(*script).run();
        } else {
            VERIFY_NOT_REACHED();
        }

        // 11. Enable outside port's port message queue.
        outside_port.start();

        // 12. If is shared is false, enable the port message queue of the worker's implicit port.
        if (!is_shared)
            m_implicit_port->start();

        // 13. If is shared is true, then queue a global task on DOM manipulation task source given worker
        //     global scope to fire an event named connect at worker global scope, using MessageEvent,
        //     with the data attribute initialized to the empty string, the ports attribute initialized
        //     to a new frozen array containing inside port, and the source attribute initialized to inside port.
        // FIXME: Shared worker support

        // 14. Enable the client message queue of the ServiceWorkerContainer object whose associated service
        //     worker client is worker global scope's relevant settings object.
        // FIXME: Understand....and support worker global settings

        // 15. Event loop: Run the responsible event loop specified by inside settings until it is destroyed.

        // 16. Clear the worker global scope's map of active timers.

        // 17. Disentangle all the ports in the list of the worker's ports.

        // 18. Empty worker global scope's owner set.
    };

    switch (options.type) {
    // -> "classic"
    case Bindings::WorkerType::Classic:
        // Fetch a classic worker script given url, outside settings, destination, and inside settings.
        fetch_classic_worker_script(realm(), url, outside_settings, destination, *m_inner_settings, move(on_complete)).release_value_but_fixme_should_propagate_errors();

    // -> "module"
    case Bindings::WorkerType::Module:
        // Fetch a module worker script graph given url, outside settings, destination, the value of the
        // credentials member of options, and inside settings.
        dbgln_if(WEB_WORKER_DEBUG, "Unsupported script type {} for LibWeb/Worker", options.type);
        TODO();
    }
}

// https://html.spec.whatwg.org/multipage/workers.html#dom-worker-terminate
WebIDL::ExceptionOr<void> Worker::terminate()
{
    dbgln_if(WEB_WORKER_DEBUG, "WebWorker: Terminate");

    return {};
}

// https://html.spec.whatwg.org/multipage/workers.html#dom-worker-postmessage
void Worker::post_message(JS::Value message, JS::Value)
{
    dbgln_if(WEB_WORKER_DEBUG, "WebWorker: Post Message: {}", message.to_string_without_side_effects());

    // 1. Let targetPort be the port with which this is entangled, if any; otherwise let it be null.
    auto& target_port = m_outside_port;

    // 2. Let options be «[ "transfer" → transfer ]».
    // 3. Run the message port post message steps providing this, targetPort, message and options.
    target_port->post_message(message);
}

#undef __ENUMERATE
#define __ENUMERATE(attribute_name, event_name)                    \
    void Worker::set_##attribute_name(WebIDL::CallbackType* value) \
    {                                                              \
        set_event_handler_attribute(event_name, move(value));      \
    }                                                              \
    WebIDL::CallbackType* Worker::attribute_name()                 \
    {                                                              \
        return event_handler_attribute(event_name);                \
    }
ENUMERATE_WORKER_EVENT_HANDLERS(__ENUMERATE)
#undef __ENUMERATE

} // namespace Web::HTML
