#pragma once

#include "Ssd1306.hpp"
#include "MainViewModel.hpp"

namespace Arcana::Lcd {

/**
 * Abstract View base class.
 * View only knows about LcdOutput (ViewModel output) and Ssd1306 (display driver).
 * View does NOT know about any Service or Observable.
 */
class BaseLcdView {
public:
    virtual ~BaseLcdView() = default;

    /// Draw static layout (title, labels, grid lines)
    virtual void onEnter(Ssd1306& display) = 0;

    /// Render dirty fields from ViewModel output
    virtual void render(Ssd1306& display, LcdOutput& output) = 0;

    /// Cleanup on view switch
    virtual void onExit(Ssd1306& display) = 0;
};

} // namespace Arcana::Lcd
