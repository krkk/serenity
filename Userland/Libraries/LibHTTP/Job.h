/*
 * Copyright (c) 2020-2022, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/HashMap.h>
#include <AK/Optional.h>
#include <LibCore/NetworkJob.h>
#include <LibCore/Socket.h>
#include <LibHTTP/HttpRequest.h>
#include <LibHTTP/HttpResponse.h>

namespace HTTP {

enum class ProtocolVersion {
    HTTP1_0_and_older,
    HTTP1_1,
    HTTP2,
};

class Job : public Core::NetworkJob {
    C_OBJECT(Job);

public:
    explicit Job(HttpRequest&&, Stream&);
    virtual ~Job() override = default;

    virtual void start(Core::BufferedSocketBase&) override;
    virtual void shutdown(ShutdownMode) override;

    Core::Socket const* socket() const { return m_socket; }
    URL url() const { return m_request.url(); }

    HttpResponse* response() { return static_cast<HttpResponse*>(Core::NetworkJob::response()); }
    HttpResponse const* response() const { return static_cast<HttpResponse const*>(Core::NetworkJob::response()); }

    void set_negotiated_version(StringView alpn)
    {
        VERIFY(m_state == State::InHeaders);

        if (alpn == "h2"sv)
            m_version = ProtocolVersion::HTTP2;
        else if (alpn == "http/1.1"sv)
            m_version = ProtocolVersion::HTTP1_1;
        else if (alpn == "http/1.0"sv || alpn == "http/0.9"sv)
            m_version = ProtocolVersion::HTTP1_0_and_older;
        else
            VERIFY_NOT_REACHED();
    }

protected:
    void finish_up();
    void on_socket_connected();
    void flush_received_buffers();
    void register_on_ready_to_read(Function<void()>);
    ErrorOr<DeprecatedString> read_line(size_t);
    ErrorOr<ByteBuffer> receive(size_t);
    void timer_event(Core::TimerEvent&) override;

    enum class State {
        InStatus,
        InHeaders,
        InBody,
        Trailers,
        Finished,
    };

    HttpRequest m_request;
    State m_state { State::InStatus };
    Core::BufferedSocketBase* m_socket { nullptr };
    ProtocolVersion m_version { ProtocolVersion::HTTP1_1 };
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
