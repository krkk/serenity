/*
 * Copyright (c) 2025, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#pragma once

#include <LibGUI/Widget.h>

namespace FileManager::Properties {

class ImageTab final : public GUI::Widget {
    C_OBJECT_ABSTRACT(ImageTab)

public:
    virtual ~ImageTab() override = default;
    static ErrorOr<NonnullRefPtr<ImageTab>> try_create();

private:
    ImageTab() = default;
};

}