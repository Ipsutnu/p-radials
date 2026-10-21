#pragma once

namespace Menu
{
    void RegisterRadialUniqueIDListener();

    // Entrada de gamepad do Quick Draw é modal: enquanto o editor estiver
    // aberto, o WheelSettings não pode reutilizar o mesmo D-pad.
    bool IsQuickDrawEditorOpen();
    bool IsQuickDrawKeyboardBrowserOpen();
    bool HandleQuickDrawGamepadPrimary();
    bool HandleQuickDrawGamepadBack();
    bool HandleQuickDrawGamepadDirectional(int horizontal, int vertical);
}
