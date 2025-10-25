/*
 * Copyright (c) 2025, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#pragma once

#include <LibGUI/Widget.h>

namespace FileManager::Properties {

class GeneralTab final : public GUI::Widget {
    C_OBJECT_ABSTRACT(GeneralTab)

public:
    virtual ~GeneralTab() override = default;
    static ErrorOr<NonnullRefPtr<GeneralTab>> try_create();

private:
    GeneralTab() = default;
};

}