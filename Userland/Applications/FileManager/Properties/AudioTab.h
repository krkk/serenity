/*
 * Copyright (c) 2025, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#pragma once

#include <LibGUI/Widget.h>

namespace FileManager::Properties {

class AudioTab final : public GUI::Widget {
    C_OBJECT_ABSTRACT(AudioTab)

public:
    virtual ~AudioTab() override = default;
    static ErrorOr<NonnullRefPtr<AudioTab>> try_create();

private:
    AudioTab() = default;
};

}