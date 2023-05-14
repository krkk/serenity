/*
 * Copyright (c) 2022-2022, Olivier De Cannière <olivier.decanniere96@gmail.com>
 * Copyright (c) 2022, Tobias Christiansen <tobyase@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibGUI/SettingsWindow.h>

class CalendarSettingsWidget final : public GUI::SettingsWindow::Tab {
    C_OBJECT(CalendarSettingsWidget)

public:
    virtual void apply_settings() override;
    virtual void reset_default_values() override;

private:
    CalendarSettingsWidget();
    Array<String, 2> const m_view_modes = { "Month"_short_string, "Year"_short_string };
    Vector<String> m_long_day_names;

    RefPtr<GUI::ComboBox> m_first_day_of_week_combobox;
    RefPtr<GUI::ComboBox> m_first_day_of_weekend_combobox;
    RefPtr<GUI::SpinBox> m_weekend_length_spinbox;
    RefPtr<GUI::ComboBox> m_default_view_combobox;
};
