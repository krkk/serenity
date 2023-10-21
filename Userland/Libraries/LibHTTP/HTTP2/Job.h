/*
 * Copyright (c) 2023, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <LibCore/NetworkJob.h>
#include <LibCore/Socket.h>
#include <LibHTTP/HttpRequest.h>
#include <LibHTTP/HttpResponse.h>

namespace HTTP::HTTP2 {

class Job : public Core::NetworkJob {
    C_OBJECT(Job);

public:
    explicit Job(HttpRequest const&, Stream&);
    virtual ~Job() override = default;

    virtual void start(Core::BufferedSocketBase&) override;
    virtual void shutdown(ShutdownMode) override;

    HttpResponse* response() { return static_cast<HttpResponse*>(Core::NetworkJob::response()); }
    HttpResponse const* response() const { return static_cast<HttpResponse const*>(Core::NetworkJob::response()); }

    const URL& url() const { return m_request.url(); }
    Core::Socket const* socket() const { return m_socket; }

    ErrorOr<size_t> response_length() const;

protected:
    void finish_up();
    void on_socket_connected();
    void flush_received_buffers();
    void register_on_ready_to_read(Function<void()>);
    bool can_read() const;
    ErrorOr<ByteBuffer> receive(size_t);
    bool write(ReadonlyBytes);

    // https://httpwg.org/specs/rfc9113.html#StreamStates
    enum class State {
        Idle,
        ReservedLocal,
        ReservedRemote,
        Open,
        HalfClosedLocal,
        HalfClosedRemote,
        Closed,
    };

    HttpRequest m_request;
    State m_state { State::Idle };
    Core::BufferedSocketBase* m_socket { nullptr };
    int m_code { -1 };
    HashMap<DeprecatedString, DeprecatedString, CaseInsensitiveStringTraits> m_headers;
    Vector<DeprecatedString> m_set_cookie_headers;

    struct ReceivedBuffer {
        ReceivedBuffer(ByteBuffer d)
            : data(move(d))
            , pending_flush(data.bytes())
        {
        }

        // The entire received buffer.
        ByteBuffer data;

        // The bytes we have yet to flush. (This is a slice of `data`)
        ReadonlyBytes pending_flush;
    };

    Vector<NonnullOwnPtr<ReceivedBuffer>> m_received_buffers;

    size_t m_buffered_size { 0 };
    size_t m_received_size { 0 };
    Optional<u64> m_content_length;
    Optional<ssize_t> m_current_chunk_remaining_size;
    Optional<size_t> m_current_chunk_total_size;
    bool m_can_stream_response { true };
    bool m_should_read_chunk_ending_line { false };
    bool m_has_scheduled_finish { false };
};

}
