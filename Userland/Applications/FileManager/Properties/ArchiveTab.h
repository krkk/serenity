/*
 * Copyright (c) 2025, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#pragma once

#include <LibGUI/Widget.h>

namespace FileManager::Properties {

class ArchiveTab final : public GUI::Widget {
    C_OBJECT_ABSTRACT(ArchiveTab)

public:
    virtual ~ArchiveTab() override = default;
    static ErrorOr<NonnullRefPtr<ArchiveTab>> try_create();

private:
    ArchiveTab() = default;
};

}