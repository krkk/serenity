/*
 * Copyright (c) 2023, Karol Kosek <krkk@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibAudio/Metadata.h>

namespace Audio {

ErrorOr<void> skip_id3(SeekableStream&);
ErrorOr<Optional<Metadata>> read_id3_metadata(SeekableStream&);

}
