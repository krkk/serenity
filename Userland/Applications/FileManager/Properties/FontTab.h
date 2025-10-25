/*
 * Copyright (c) 2025, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#pragma once

#include <LibGUI/Widget.h>

namespace FileManager::Properties {

class FontTab final : public GUI::Widget {
    C_OBJECT_ABSTRACT(FontTab)

public:
    virtual ~FontTab() override = default;
    static ErrorOr<NonnullRefPtr<FontTab>> try_create();

private:
    FontTab() = default;
};

}