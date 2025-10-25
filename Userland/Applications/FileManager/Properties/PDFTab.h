/*
 * Copyright (c) 2025, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#pragma once

#include <LibGUI/Widget.h>

namespace FileManager::Properties {

class PDFTab final : public GUI::Widget {
    C_OBJECT_ABSTRACT(PDFTab)

public:
    static ErrorOr<NonnullRefPtr<PDFTab>> try_create();
    virtual ~PDFTab() override = default;

private:
    PDFTab() = default;
};

}