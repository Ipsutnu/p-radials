#include "PCH.h"

#include "Input.h"
#include "ItemInfo.h"
#include "Config.h"
#include "Resolution.h"
#include "Gamepad.h"
#include "Animation/Track/TrackEditor.h"
#include "Printscreen.h"

namespace
{
    bool g_settingsOwnsLeftClick = false;
    bool g_settingsOwnsRightClick = false;
    bool g_primaryActivationHeld = false;
    bool g_secondaryActivationHeld = false;
    bool g_automaticArrowHeld = false;
    float g_settingsLeftTriggerNextRepeat = 0.35f;
    float g_settingsRightTriggerNextRepeat = 0.35f;
    float g_gamepadDPadUpNextRepeat = 0.32f;
    float g_gamepadDPadDownNextRepeat = 0.32f;
    float g_gamepadDPadLeftNextRepeat = 0.32f;
    float g_gamepadDPadRightNextRepeat = 0.32f;

    bool ShouldRepeatSettingsTrigger(RE::ButtonEvent* button, float& nextRepeat)
    {
        if (!button->IsPressed())
        {
            nextRepeat = 0.35f;
            return false;
        }
        const float held = button->GetRuntimeData().heldDownSecs;
        if (button->IsDown() && held <= 0.0f)
        {
            nextRepeat = 0.35f;
            return true;
        }
        if (held < nextRepeat)
            return false;
        do
        {
            nextRepeat += 0.11f;
        } while (nextRepeat <= held);
        return true;
    }

    bool ShouldRepeatGameplayDPad(RE::ButtonEvent* button, float& nextRepeat)
    {
        if (!button->IsPressed())
        {
            nextRepeat = 0.32f;
            return false;
        }

        const float held = button->GetRuntimeData().heldDownSecs;
        if (button->IsDown() && held <= 0.0f)
        {
            nextRepeat = 0.32f;
            return true;
        }
        if (held < nextRepeat)
            return false;

        do
        {
            nextRepeat += 0.12f;
        } while (nextRepeat <= held);
        return true;
    }

    std::uint32_t VirtualKeyToSkyrimScan(int virtualKey)
    {
        const UINT scan = MapVirtualKeyW(
            static_cast<UINT>(virtualKey), MAPVK_VK_TO_VSC_EX);
        const UINT prefix = scan & 0xFF00u;
        const UINT code = scan & 0xFFu;

        // Skyrim/DirectInput representa as teclas estendidas E0 usando o bit
        // alto do scan code (por exemplo, Up = 0xC8 em vez de 0x48).
        return prefix == 0xE000u ? code | 0x80u : code;
    }

    bool MatchesKeyboardVirtualKey(std::uint32_t eventScan, int virtualKey)
    {
        const UINT mapped = MapVirtualKeyW(
            static_cast<UINT>(virtualKey), MAPVK_VK_TO_VSC_EX);
        const std::uint32_t rawScan = mapped & 0xFFu;
        const std::uint32_t skyrimScan = VirtualKeyToSkyrimScan(virtualKey);
        if (eventScan == rawScan || eventScan == skyrimScan)
            return true;

        // Alguns runtimes entregam as teclas E0 já no formato DirectInput
        // (bit 7 ligado), enquanto outros mantêm o scan code base. Converte
        // ambos de volta para VK para a comparação não depender do backend.
        UINT scanForWindows = eventScan;
        if ((eventScan & 0x80u) != 0)
            scanForWindows = 0xE000u | (eventScan & 0x7Fu);
        const UINT eventVirtualKey = MapVirtualKeyW(
            scanForWindows, MAPVK_VSC_TO_VK_EX);
        return eventVirtualKey == static_cast<UINT>(virtualKey);
    }

    bool AnyActivationHeld()
    {
        return g_primaryActivationHeld || g_secondaryActivationHeld || g_automaticArrowHeld;
    }

    void UpdateActivationState(bool previous)
    {
        const bool current = AnyActivationHeld();
        Gamepad::SetActivationHeld(current);
        if (!previous && current)
        {
            if (Menu::IsRadialLockedOpen() || Menu::CanDrawWheelMenu())
                Menu::HandleGPressed();
        }
        else if (previous && !current)
        {
            Menu::HandleGReleased();
        }
    }
}

InputHandler* InputHandler::GetSingleton()
{
    static InputHandler instance;
    return &instance;
}

void ResetWheelInputState()
{
    g_primaryActivationHeld = false;
    g_secondaryActivationHeld = false;
    g_automaticArrowHeld = false;
    g_settingsOwnsLeftClick = false;
    g_settingsOwnsRightClick = false;
    g_settingsLeftTriggerNextRepeat = 0.35f;
    g_settingsRightTriggerNextRepeat = 0.35f;
    g_gamepadDPadUpNextRepeat = 0.32f;
    g_gamepadDPadDownNextRepeat = 0.32f;
    g_gamepadDPadLeftNextRepeat = 0.32f;
    g_gamepadDPadRightNextRepeat = 0.32f;
    Gamepad::ResetRuntimeState();
}

void RegisterInputSink()
{
    auto* deviceManager = RE::BSInputDeviceManager::GetSingleton();
    if (!deviceManager)
        return;

    // Registra o nosso sink. A prioridade definitiva é aplicada logo abaixo.
    deviceManager->AddEventSink(InputHandler::GetSingleton());

    MaintainInputSinkPriority();
}

void MaintainInputSinkPriority()
{
    auto* deviceManager = RE::BSInputDeviceManager::GetSingleton();
    if (!deviceManager)
        return;

    // A lista é usada pelo próprio Skyrim ao distribuir eventos. Nunca a
    // reorganizamos durante essa distribuição; o próximo frame a corrige.
    RE::BSSpinLockGuard locker(deviceManager->lock);
    if (deviceManager->notifying)
        return;

    auto& sinks = deviceManager->sinks;
    auto* const handler = InputHandler::GetSingleton();

    auto it = std::find(sinks.begin(), sinks.end(), handler);
    if (it == sinks.end())
    {
        // Alguns ciclos de reinicialização de input recriam a lista de sinks.
        // Reinsere o handler já na frente para que o primeiro mouse move seja
        // bloqueado antes de chegar ao PlayerControls.
        sinks.push_front(handler);
        return;
    }

    if (it != sinks.begin())
    {
        // O handler precisa rodar antes do PlayerControls, pois o bloqueio
        // altera o próprio MouseMoveEvent (X/Y = 0).
        std::rotate(sinks.begin(), it, std::next(it));
    }
}

void InputHandler::Register()
{
    auto* inputManager = RE::BSInputDeviceManager::GetSingleton();
    if (inputManager)
    {
        inputManager->AddEventSink(GetSingleton());
    }
}

RE::BSEventNotifyControl InputHandler::ProcessEvent(
    RE::InputEvent* const* a_event,
    RE::BSTEventSource<RE::InputEvent*>*)
{
    if (!a_event || !*a_event)
        return RE::BSEventNotifyControl::kContinue;

    // Iteração pelos eventos de entrada
    for (auto* event = *a_event; event; event = event->next)
    {
        if (!event)
            continue;

        if (event->eventType == RE::INPUT_EVENT_TYPE::kButton)
        {
            auto* button = event->AsButtonEvent();

            if (button)
            {
                // O Keyboard Hub consome as teclas antes do fluxo normal,
                // inclusive em runtimes que codificam PrintScreen como scan
                // estendido. Capture-o aqui, antes do handler modal, usando
                // a mesma comparação tolerante a backend usada no restante
                // do Input.
                if (Menu::IsQuickDrawKeyboardBrowserOpen() &&
                    button->GetDevice() == RE::INPUT_DEVICE::kKeyboard &&
                    button->IsDown() &&
                    button->GetRuntimeData().heldDownSecs <= 0.0f &&
                    MatchesKeyboardVirtualKey(button->GetIDCode(), VK_SNAPSHOT))
                {
                    Printscreen::Request();
                    button->GetRuntimeData().value = 0.0f;
                    button->GetRuntimeData().heldDownSecs = 0.0f;
                    button->SetUserEvent("");
                    button->SetIDCode(0xFF);
                    continue;
                }

                // Enquanto o diálogo de nome do preset do Custom Radial está
                // aberto, ele é um campo de texto modal. Isso vem antes dos
                // atalhos globais para que, por exemplo, "X" seja digitado
                // no nome em vez de fechar/alternar a interface.
                if (button->GetDevice() == RE::INPUT_DEVICE::kKeyboard &&
                    button->IsDown() &&
                    button->GetRuntimeData().heldDownSecs <= 0.0f &&
                    TrackEditor::KeyboardInput(button->GetIDCode(), true))
                {
                    button->GetRuntimeData().value = 0.0f;
                    button->GetRuntimeData().heldDownSecs = 0.0f;
                    button->SetUserEvent("");
                    button->SetIDCode(0xFF);
                    continue;
                }

                // O nome do loadout no Armor Hub é outro campo modal. Ele
                // precisa vir antes dos atalhos globais pelo mesmo motivo do
                // nome de preset: letras como First/Second são texto aqui.
                if (button->GetDevice() == RE::INPUT_DEVICE::kKeyboard)
                {
                    const bool firstPress = button->IsDown() &&
                        button->GetRuntimeData().heldDownSecs <= 0.0f;
                    if (Menu::QuickDrawArmorKeyboardInput(button->GetIDCode(), firstPress))
                    {
                        button->GetRuntimeData().value = 0.0f;
                        button->GetRuntimeData().heldDownSecs = 0.0f;
                        button->SetUserEvent("");
                        button->SetIDCode(0xFF);
                        continue;
                    }
                }

                // O nome de layout do WheelSettings também é um campo modal.
                // Isto precisa ficar neste primeiro loop, antes da leitura de
                // First/Second Key logo abaixo; o segundo loop já recebe o
                // evento tarde demais, depois que o atalho global foi usado.
                if (button->GetDevice() == RE::INPUT_DEVICE::kKeyboard)
                {
                    const bool firstPress = button->IsDown() &&
                        button->GetRuntimeData().heldDownSecs <= 0.0f;
                    if (Menu::SettingsLayoutKeyboardInput(button->GetIDCode(), firstPress))
                    {
                        button->GetRuntimeData().value = 0.0f;
                        button->GetRuntimeData().heldDownSecs = 0.0f;
                        button->SetUserEvent("");
                        button->SetIDCode(0xFF);
                        continue;
                    }
                }

                // ============================================================
                // CAPTURA DA TECLA NO SETTINGS
                // ============================================================

                if (button &&
                    Menu::CaptureWheelKey(button))
                {
                    // Consome a tecla capturada para impedir que
                    // o Skyrim ou o p-radials a processe normalmente.

                    button->GetRuntimeData().value = 0.0f;

                    button->GetRuntimeData().heldDownSecs = 0.0f;

                    button->SetUserEvent("");

                    button->SetIDCode(0xFF);

                    continue;
                }   

                if (button && Menu::CaptureQuickDrawHotkey(button))
                {
                    button->GetRuntimeData().value = 0.0f;
                    button->GetRuntimeData().heldDownSecs = 0.0f;
                    button->SetUserEvent("");
                    button->SetIDCode(0xFF);
                    continue;
                }

                // ============================================================
                // ATUALIZA SCAN CODE DA TECLA CONFIGURADA IMEDIATAMENTE
                // ============================================================

                if (button->GetDevice() == RE::INPUT_DEVICE::kKeyboard)
                {
                    const int virtualKey = static_cast<int>(MapVirtualKeyW(
                        static_cast<UINT>(button->GetIDCode()), MAPVK_VSC_TO_VK_EX));
                    const bool firstPress = button->IsDown() &&
                        button->GetRuntimeData().heldDownSecs <= 0.0f;
                    const bool released = !button->IsPressed();
                    if ((released && Menu::HandleQuickDrawHotkeyRelease(virtualKey)) ||
                        Menu::HandleQuickDrawHotkeyHandKey(virtualKey, firstPress) ||
                        (firstPress && Menu::HandleQuickDrawHotkey(virtualKey,
                            g_primaryActivationHeld || g_secondaryActivationHeld)))
                    {
                        button->GetRuntimeData().value = 0.0f;
                        button->GetRuntimeData().heldDownSecs = 0.0f;
                        button->SetUserEvent("");
                        button->SetIDCode(0xFF);
                        continue;
                    }
                    const bool previousDeviceState = AnyActivationHeld();
                    if (Gamepad::IsBinding(Config::g_toggleKey))
                        g_primaryActivationHeld = false;
                    if (Gamepad::IsBinding(Config::g_secondaryKey))
                        g_secondaryActivationHeld = false;
                    if (previousDeviceState != AnyActivationHeld())
                        UpdateActivationState(previousDeviceState);

                    const auto matchesKeyboardBinding = [&](int binding) {
                        if (binding == 0 || Gamepad::IsBinding(binding))
                            return false;
                        return MatchesKeyboardVirtualKey(
                            button->GetIDCode(), binding);
                    };
                    const bool primary = matchesKeyboardBinding(Config::g_toggleKey);
                    const bool secondary = matchesKeyboardBinding(Config::g_secondaryKey);
                    const bool altConfig = matchesKeyboardBinding(Config::g_altConfigKey);
                    // A tecla direta não toma prioridade quando foi configurada
                    // também como uma das teclas modificadoras do radial.
                    if (altConfig && !primary && !secondary)
                    {
                        if (button->IsDown() && button->GetRuntimeData().heldDownSecs <= 0.0f)
                            Menu::ToggleSettingsMenuFromAltKey();
                        button->GetRuntimeData().value = 0.0f;
                        button->GetRuntimeData().heldDownSecs = 0.0f;
                        button->SetUserEvent("");
                        button->SetIDCode(0xFF);
                        continue;
                    }
                    if (primary || secondary)
                    {
                        // Sobre o botão de hotkey do Quick Draw, a primeira
                        // ou segunda tecla inicia a captura em vez de
                        // confirmar/fechar o editor.
                        if (button->IsDown() &&
                            button->GetRuntimeData().heldDownSecs <= 0.0f &&
                            Menu::BeginQuickDrawHotkeyCaptureAtCursor())
                        {
                            button->GetRuntimeData().value = 0.0f;
                            button->GetRuntimeData().heldDownSecs = 0.0f;
                            button->SetUserEvent("");
                            button->SetIDCode(0xFF);
                            continue;
                        }
                        const bool previous = AnyActivationHeld();
                        if (button->IsDown() && button->GetRuntimeData().heldDownSecs <= 0.0f)
                        {
                            if (primary) g_primaryActivationHeld = true;
                            if (secondary) g_secondaryActivationHeld = true;
                        }
                        else if (!button->IsPressed())
                        {
                            if (primary) g_primaryActivationHeld = false;
                            if (secondary) g_secondaryActivationHeld = false;
                        }
                        UpdateActivationState(previous);
                        button->GetRuntimeData().value = 0.0f;
                        button->GetRuntimeData().heldDownSecs = 0.0f;
                        button->SetUserEvent("");
                        button->SetIDCode(0xFF);
                    }
                }
            }
        }
    }

    auto consumeButton = [](RE::ButtonEvent* button) {
        button->GetRuntimeData().value = 0.0f;
        button->GetRuntimeData().heldDownSecs = 0.0f;
        button->SetUserEvent("");
        button->SetIDCode(0xFF);
    };

    auto* gamepadUI = RE::UI::GetSingleton();
    const bool gamepadSettingsOpen =
        gamepadUI && gamepadUI->IsMenuOpen("WheelSetting");

    for (auto* event = *a_event; event; event = event->next)
    {
        if (!event)
            continue;

        if (auto* stick = event->AsThumbstickEvent())
        {
            const bool wheelOwnsStick = gamepadSettingsOpen ||
                Gamepad::IsActivationHeld() || g_showWindow;
            if (!wheelOwnsStick)
                continue;

            const bool rightStick =
                stick->GetIDCode() == RE::ThumbstickEvent::InputType::kRightThumbstick;
            const float lengthSq = stick->xValue * stick->xValue + stick->yValue * stick->yValue;

            if (!gamepadSettingsOpen && Gamepad::IsActivationHeld() &&
                g_radialMode == RadialMode::Gameplay && g_radialSide == RadialSide::None &&
                lengthSq > 0.36f)
            {
                Menu::GamepadOpenRadial(rightStick ? RadialSide::Right : RadialSide::Left);
            }
            else
            {
                Menu::GamepadMoveCursor(stick->xValue, stick->yValue, rightStick);
            }

            stick->xValue = 0.0f;
            stick->yValue = 0.0f;
            continue;
        }

        auto* button = event->AsButtonEvent();
        if (!button || button->GetDevice() != RE::INPUT_DEVICE::kGamepad)
            continue;

        const std::uint32_t id = button->GetIDCode();
        const bool previousDeviceState = AnyActivationHeld();
        if (!Gamepad::IsBinding(Config::g_toggleKey))
            g_primaryActivationHeld = false;
        if (!Gamepad::IsBinding(Config::g_secondaryKey))
            g_secondaryActivationHeld = false;
        if (previousDeviceState != AnyActivationHeld())
            UpdateActivationState(previousDeviceState);
        const bool primaryBinding = Gamepad::Matches(Config::g_toggleKey, id);
        const bool secondaryBinding = Gamepad::Matches(Config::g_secondaryKey, id);
        const bool altConfigBinding = Gamepad::Matches(Config::g_altConfigKey, id);
        // A tecla configurada sempre tem prioridade sobre qualquer comando
        // nativo do botão (incluindo L3/R3) e é consumida como modificadora.
        if (primaryBinding || secondaryBinding)
        {
            const bool previous = AnyActivationHeld();
            if (button->IsDown() && button->GetRuntimeData().heldDownSecs <= 0.0f)
            {
                if (primaryBinding) g_primaryActivationHeld = true;
                if (secondaryBinding) g_secondaryActivationHeld = true;
            }
            else if (!button->IsPressed())
            {
                if (primaryBinding) g_primaryActivationHeld = false;
                if (secondaryBinding) g_secondaryActivationHeld = false;
            }
            UpdateActivationState(previous);
            consumeButton(button);
            continue;
        }

        const bool firstPress = button->IsDown() &&
            button->GetRuntimeData().heldDownSecs <= 0.0f;
        if (altConfigBinding && !primaryBinding && !secondaryBinding)
        {
            if (firstPress)
                Menu::ToggleSettingsMenuFromAltKey();
            consumeButton(button);
            continue;
        }
        const bool wheelOwnsButton = gamepadSettingsOpen ||
            Gamepad::IsActivationHeld() || g_showWindow;
        if (!wheelOwnsButton)
            continue;

        using Key = RE::BSWin32GamepadDevice::Key;
        bool handled = false;

        if (Menu::IsQuickDrawEditorOpen())
        {
            if (id == Key::kA || id == Key::kX)
            {
                if (firstPress)
                    handled = Menu::HandleQuickDrawGamepadPrimary();
            }
            else if (firstPress && id == Key::kB)
                handled = Menu::HandleQuickDrawGamepadBack();
            else if (firstPress && id == Key::kUp)
                handled = Menu::HandleQuickDrawGamepadDirectional(0, -1);
            else if (firstPress && id == Key::kDown)
                handled = Menu::HandleQuickDrawGamepadDirectional(0, 1);
            else if (firstPress && id == Key::kLeft)
                handled = Menu::HandleQuickDrawGamepadDirectional(-1, 0);
            else if (firstPress && id == Key::kRight)
                handled = Menu::HandleQuickDrawGamepadDirectional(1, 0);
        }
        else if (Menu::IsQuickDrawKeyboardBrowserOpen())
        {
            // Hub e Armor Hub terão navegação própria; por ora nenhum D-pad
            // pode vazar para o cursor global do WheelSettings.
            handled = firstPress && (id == Key::kUp || id == Key::kDown ||
                id == Key::kLeft || id == Key::kRight);
        }
        else if (gamepadSettingsOpen)
        {
            if (id == Key::kA || id == Key::kX)
            {
                if (firstPress)
                {
                    Menu::GamepadSettingsPrimaryDown();
                    handled = true;
                }
                else if (!button->IsPressed())
                {
                    Menu::GamepadSettingsPrimaryUp();
                    handled = true;
                }
            }
            else if (firstPress && id == Key::kB)
            {
                Menu::GamepadSettingsDelete();
                handled = true;
            }
            else if (firstPress && id == Key::kY)
            {
                Menu::GamepadSettingsClose(); handled = true;
            }
            else if (firstPress && id == Key::kUp)
            {
                Menu::GamepadSettingsDirectional(0, -1); handled = true;
            }
            else if (firstPress && id == Key::kDown)
            {
                Menu::GamepadSettingsDirectional(0, 1); handled = true;
            }
            else if (firstPress && id == Key::kLeft)
            {
                Menu::GamepadSettingsDirectional(-1, 0); handled = true;
            }
            else if (firstPress && id == Key::kRight)
            {
                Menu::GamepadSettingsDirectional(1, 0); handled = true;
            }
            else if (firstPress && id == Key::kLeftShoulder)
            {
                Menu::GamepadSettingsAdjustSlider(-1); handled = true;
            }
            else if (firstPress && id == Key::kRightShoulder)
            {
                Menu::GamepadSettingsAdjustSlider(1); handled = true;
            }
            else if (id == Key::kLeftTrigger)
            {
                if (ShouldRepeatSettingsTrigger(button, g_settingsLeftTriggerNextRepeat))
                    Menu::GamepadSettingsCycleRadial(-1);
                handled = true;
            }
            else if (id == Key::kRightTrigger)
            {
                if (ShouldRepeatSettingsTrigger(button, g_settingsRightTriggerNextRepeat))
                    Menu::GamepadSettingsCycleRadial(1);
                handled = true;
            }
        }
        else if (Gamepad::IsActivationHeld() ||
            (g_showWindow && g_radialSide != RadialSide::None))
        {
            float* dpadRepeat = nullptr;
            if (id == Key::kUp) dpadRepeat = &g_gamepadDPadUpNextRepeat;
            else if (id == Key::kDown) dpadRepeat = &g_gamepadDPadDownNextRepeat;
            else if (id == Key::kLeft) dpadRepeat = &g_gamepadDPadLeftNextRepeat;
            else if (id == Key::kRight) dpadRepeat = &g_gamepadDPadRightNextRepeat;
            const bool dpadStep = dpadRepeat &&
                ShouldRepeatGameplayDPad(button, *dpadRepeat);
            const bool dpadPressed = dpadRepeat && button->IsDown();

            if (Gamepad::IsActivationHeld() && firstPress &&
                (id == Key::kLeftThumb || id == Key::kRightThumb))
            {
                Menu::OpenSettingsMenu(); handled = true;
            }
            else if (g_radialMode == RadialMode::Inventory &&
                (id == Key::kUp || id == Key::kDown || id == Key::kLeft || id == Key::kRight))
            {
                const float dx = id == Key::kLeft ? -1.0f : id == Key::kRight ? 1.0f : 0.0f;
                const float dy = id == Key::kUp ? 1.0f : id == Key::kDown ? -1.0f : 0.0f;
                Menu::GamepadMoveCursor(dx, dy, false); handled = true;
            }
            else if (Gamepad::IsActivationHeld() && dpadPressed &&
                g_radialSide == RadialSide::None &&
                (id == Key::kLeft || id == Key::kRight))
            {
                handled = Menu::GamepadOpenRadial(
                    id == Key::kLeft ? RadialSide::Left : RadialSide::Right);
            }
            else if (Gamepad::IsActivationHeld() && dpadPressed &&
                g_radialSide == RadialSide::None &&
                (id == Key::kUp || id == Key::kDown))
            {
                const bool hasTop = !Menu::g_topItems.empty();
                const bool hasBottom = !Menu::g_bottomItems.empty();
                RadialSide target = RadialSide::None;
                if (hasTop && hasBottom)
                    target = id == Key::kUp ? RadialSide::Top : RadialSide::Bottom;
                else if (hasTop)
                    target = RadialSide::Top;
                else if (hasBottom)
                    target = RadialSide::Bottom;
                handled = target != RadialSide::None &&
                    Menu::GamepadOpenRadial(target);
            }
            else if (dpadStep &&
                (g_radialSide == RadialSide::Left ||
                 g_radialSide == RadialSide::Right) &&
                (id == Key::kLeft || id == Key::kRight))
            {
                Menu::GamepadStepSideSelection(id == Key::kLeft ? -1 : 1);
                handled = true;
            }
            else if (dpadStep &&
                (g_radialSide == RadialSide::Top ||
                 g_radialSide == RadialSide::Bottom) &&
                (id == Key::kUp || id == Key::kDown))
            {
                // Em ambos os radiais verticais, Up avança e Down volta.
                Menu::GamepadShoulder(id == Key::kUp ? 1 : -1);
                handled = true;
            }
            else if (dpadPressed && (id == Key::kUp || id == Key::kDown))
            {
                const bool hasTop = !Menu::g_topItems.empty();
                const bool hasBottom = !Menu::g_bottomItems.empty();
                RadialSide target = RadialSide::None;
                if (hasTop && hasBottom)
                    target = id == Key::kUp ? RadialSide::Top : RadialSide::Bottom;
                else if (hasTop)
                    target = RadialSide::Top;
                else if (hasBottom)
                    target = RadialSide::Bottom;
                handled = target != RadialSide::None &&
                    Menu::GamepadOpenRadial(target);
            }
            else if (firstPress && id == Key::kLeft &&
                g_radialSide != RadialSide::None)
            {
                Menu::GamepadShoulder(-1); handled = true;
            }
            else if (firstPress && id == Key::kRight &&
                g_radialSide != RadialSide::None)
            {
                Menu::GamepadShoulder(1); handled = true;
            }
            else if (firstPress && id == Key::kLeftShoulder)
            {
                Menu::GamepadShoulder(1); handled = true;
            }
            else if (firstPress && id == Key::kRightShoulder)
            {
                Menu::GamepadShoulder(-1); handled = true;
            }
        }

        if (handled)
            consumeButton(button);
    }

    if (!gamepadSettingsOpen)
    {
        for (auto* event = *a_event; event; event = event->next)
        {
            auto* button = event ? event->AsButtonEvent() : nullptr;
            if (!button || button->GetDevice() != RE::INPUT_DEVICE::kKeyboard)
                continue;

            RadialSide side = RadialSide::None;
            if (MatchesKeyboardVirtualKey(button->GetIDCode(), VK_UP)) side = RadialSide::Top;
            else if (MatchesKeyboardVirtualKey(button->GetIDCode(), VK_DOWN)) side = RadialSide::Bottom;
            else if (MatchesKeyboardVirtualKey(button->GetIDCode(), VK_LEFT)) side = RadialSide::Left;
            else if (MatchesKeyboardVirtualKey(button->GetIDCode(), VK_RIGHT)) side = RadialSide::Right;
            else if (MatchesKeyboardVirtualKey(button->GetIDCode(), VK_NUMPAD8))
                side = RadialSide::Top;
            else if (MatchesKeyboardVirtualKey(button->GetIDCode(), VK_NUMPAD2))
                side = RadialSide::Bottom;
            else if (MatchesKeyboardVirtualKey(button->GetIDCode(), VK_NUMPAD4))
                side = RadialSide::Left;
            else if (MatchesKeyboardVirtualKey(button->GetIDCode(), VK_NUMPAD6))
                side = RadialSide::Right;
            if (side == RadialSide::None)
                continue;

            const bool physicalActivationHeld =
                g_primaryActivationHeld || g_secondaryActivationHeld;
            const bool firstPress = button->IsDown() &&
                button->GetRuntimeData().heldDownSecs <= 0.0f;
            bool handled = false;

            // Uma única opção controla tanto as setas dedicadas quanto
            // 8/2/4/6 do Numpad.
            const bool automaticOpen = Config::g_automaticArrowMenus;
            if (firstPress && (physicalActivationHeld || automaticOpen))
            {
                if (!physicalActivationHeld && automaticOpen)
                {
                    const bool previous = AnyActivationHeld();
                    g_automaticArrowHeld = true;
                    UpdateActivationState(previous);
                }
                handled = Menu::GamepadOpenRadial(side) || g_showWindow;
            }
            else if (!button->IsPressed() && g_automaticArrowHeld && automaticOpen)
            {
                const bool previous = AnyActivationHeld();
                g_automaticArrowHeld = false;
                UpdateActivationState(previous);
                handled = true;
            }
            else if (physicalActivationHeld)
            {
                handled = true;
            }

            if (handled)
                consumeButton(button);
        }
    }


    // Se o menu do Skyrim estiver aberto (inventário, pausa, etc.), não bloqueia nada
    //auto* ui = RE::UI::GetSingleton();
    //if (ui && ui->IsItemMenuOpen())
    //{
    //    return RE::BSEventNotifyControl::kContinue;
    //}

    auto* ui = RE::UI::GetSingleton();

    const bool settingsOpen =
        ui && ui->IsMenuOpen("WheelSetting");

    if (!settingsOpen)
    {
        g_settingsOwnsLeftClick = false;
        g_settingsOwnsRightClick = false;
    }

    // Hotkeys do Quick Draw vivem fora do radial normal. Esta captura precisa
    // acontecer antes do retorno antecipado abaixo, que é comum durante o
    // preview direto. Assim a roda nunca alcança os controles do Skyrim
    // enquanto a tecla do atalho continua pressionada.
    for (auto* event = *a_event; event; event = event->next)
    {
        auto* button = event ? event->AsButtonEvent() : nullptr;
        if (!button || button->GetDevice() != RE::INPUT_DEVICE::kMouse)
            continue;

        const int mouseButton = static_cast<int>(button->GetIDCode());
        if ((mouseButton == 8 || mouseButton == 9) &&
            button->GetRuntimeData().value != 0.0f)
        {
            const int direction = mouseButton == 8 ? 1 : -1;
            if (Menu::ScrollQuickDrawHotkeyHud(direction))
                consumeButton(button);
        }
    }

    // A escolha de mão iniciada por um atalho é modal mesmo sem radial
    // aberto: o primeiro movimento horizontal ou clique pertence ao Wheel.
    if (Menu::IsQuickDrawHotkeyHandSelectionActive())
    {
        for (auto* event = *a_event; event; event = event->next)
        {
            if (event && event->GetEventType() == RE::INPUT_EVENT_TYPE::kMouseMove)
            {
                auto* move = static_cast<RE::MouseMoveEvent*>(event);
                if (Menu::HandleQuickDrawHotkeyHandMouseMove(move->mouseInputX))
                {
                    move->mouseInputX = 0;
                    move->mouseInputY = 0;
                }
                continue;
            }
            auto* button = event ? event->AsButtonEvent() : nullptr;
            if (!button || button->GetDevice() != RE::INPUT_DEVICE::kMouse)
                continue;
            const int mouseButton = static_cast<int>(button->GetIDCode());
            // Este caminho é modal enquanto a tecla do atalho está pressionada,
            // inclusive quando não há nenhum menu aberto. Interceptar a roda
            // aqui impede que o Skyrim receba o scroll antes do HUD.
            if ((mouseButton == 8 || mouseButton == 9) &&
                button->GetRuntimeData().value != 0.0f)
            {
                const int direction = mouseButton == 8 ? 1 : -1;
                if (Menu::ScrollQuickDrawHotkeyHud(direction))
                {
                    consumeButton(button);
                    continue;
                }
            }
            const bool firstPress = button->IsDown() &&
                button->GetRuntimeData().heldDownSecs <= 0.0f;
            if (Menu::HandleQuickDrawHotkeyHandMouseButton(
                    mouseButton, firstPress))
                consumeButton(button);
        }
    }

    // O HUD de hotkeys e o Keyboard Hub são interfaces do Wheel mesmo sem
    // abrir o radial normal. Mantê-los neste fluxo permite que PrintScreen
    // seja composto pelo nosso sistema enquanto qualquer um estiver visível.
    const bool hotkeyHudVisible = Menu::IsQuickDrawHotkeyHudVisible();
    const bool keyboardHubOpen = Menu::IsQuickDrawKeyboardBrowserOpen();
    if (!g_showWindow && !settingsOpen && !hotkeyHudVisible && !keyboardHubOpen)
    {
        return RE::BSEventNotifyControl::kContinue;
    }

    // Processamento dos Eventos de Input
    for (auto* event = *a_event; event; event = event->next)
    {
        if (!event)
            continue;

        // O Wheel assume PrintScreen apenas quando alguma de suas interfaces
        // está visível. Em qualquer outra situação o evento segue intacto
        // para o screenshot nativo do Skyrim.
        if (auto* screenshotKey = event->AsButtonEvent(); screenshotKey &&
            screenshotKey->GetDevice() == RE::INPUT_DEVICE::kKeyboard &&
            screenshotKey->IsDown() &&
            screenshotKey->GetRuntimeData().heldDownSecs <= 0.0f &&
            MatchesKeyboardVirtualKey(screenshotKey->GetIDCode(), VK_SNAPSHOT))
        {
            Printscreen::Request();
            consumeButton(screenshotKey);
            continue;
        }

        if (settingsOpen)
        {
            auto* layoutKey = event->AsButtonEvent();
            if (layoutKey && layoutKey->GetDevice() == RE::INPUT_DEVICE::kKeyboard)
            {
                const bool firstPress = layoutKey->IsDown() &&
                    layoutKey->GetRuntimeData().heldDownSecs <= 0.0f;
                if (Menu::QuickDrawArmorKeyboardInput(layoutKey->GetIDCode(), firstPress))
                {
                    consumeButton(layoutKey);
                    continue;
                }
                if (Menu::SettingsLayoutKeyboardInput(layoutKey->GetIDCode(), firstPress))
                {
                    consumeButton(layoutKey);
                    continue;
                }
            }
        }

        if (settingsOpen && TrackEditor::IsOpen())
        {
            auto* editorKey = event->AsButtonEvent();
            if (editorKey && editorKey->GetDevice() == RE::INPUT_DEVICE::kKeyboard)
            {
                const bool editorFirstPress = editorKey->IsDown() &&
                    editorKey->GetRuntimeData().heldDownSecs <= 0.0f;
                if (TrackEditor::KeyboardInput(editorKey->GetIDCode(), editorFirstPress))
                {
                    consumeButton(editorKey);
                    continue;
                }
                const bool space = MatchesKeyboardVirtualKey(
                    editorKey->GetIDCode(), VK_SPACE);
                const bool control = MatchesKeyboardVirtualKey(
                    editorKey->GetIDCode(), VK_CONTROL) ||
                    MatchesKeyboardVirtualKey(editorKey->GetIDCode(), VK_LCONTROL) ||
                    MatchesKeyboardVirtualKey(editorKey->GetIDCode(), VK_RCONTROL);
                if (space || control)
                {
                    if (space) TrackEditor::SetPanModifier(editorKey->IsPressed());
                    if (control) TrackEditor::SetZoomModifier(editorKey->IsPressed());
                    consumeButton(editorKey);
                    continue;
                }
            }
        }

        // ============================================================
        // A. MOVIMENTO DO MOUSE
        // ============================================================

        if (event->GetEventType() ==
            RE::INPUT_EVENT_TYPE::kMouseMove)
        {
            auto* mouseMove =
                static_cast<RE::MouseMoveEvent*>(event);

            auto* ui = RE::UI::GetSingleton();

            const bool settingsOpen =
                ui && ui->IsMenuOpen("WheelSetting");

            // ========================================================
            // SETTINGS
            // ========================================================

            if (settingsOpen)
            {
                Menu::UsePhysicalSettingsCursor();
                // O Hub sempre é dono do movimento do mouse. A opção de
                // pausa decide somente o estado do jogo; ela não pode deixar
                // a câmera receber deltas quando o menu está na tela.
                mouseMove->mouseInputX = 0;
                mouseMove->mouseInputY = 0;
                continue;
            }

            // ========================================================
            // INVENTÁRIO
            // ========================================================

            if (g_radialMode == RadialMode::Inventory &&
                g_inventoryDragMode == InventoryDragMode::KeyDrag)
            {
                // O modo normal acompanha a posição absoluta do Scaleform.
                // Fast Drag, quando ativo, preserva temporariamente a zona
                // da última soltura válida e usa o delta apenas dentro dela.
                Menu::UpdateInventoryDragPointerFromSkyrimMouse();
                continue;
            }

            // ========================================================
            // GAMEPLAY - LIBERA MOVIMENTO DO MOUSE
            // ========================================================

            if (Menu::IsCurrentRadialMouseUnlocked())
            {
                // Não zera os deltas.
                // Permite que o Skyrim receba o movimento original.
                continue;
            }

            // ========================================================
            // GAMEPLAY - BLOQUEIA MOVIMENTO DA CÂMERA
            // ========================================================

            mouseMove->mouseInputX = 0;
            mouseMove->mouseInputY = 0;

            continue;
        }

        // ------------------------------------------------------------
        // B. BLOQUEIO E LEITURA DE SCROLL DO MOUSE (Button 8 e 9)
        // ------------------------------------------------------------

        auto* buttonEvent = event->AsButtonEvent();

        if (!buttonEvent)
            continue;

        if (buttonEvent->GetDevice() == RE::INPUT_DEVICE::kMouse)
        {
            const uint32_t mouseButton =
                buttonEvent->GetIDCode();

            // O editor de Quick Draw é modal mesmo quando o WheelSettings
            // principal já não está aberto. Trata o clique antes do fluxo de
            // gameplay, que de outro modo o consumia como início de desenho.
            if ((Menu::IsQuickDrawEditorOpen() ||
                    Menu::IsQuickDrawKeyboardBrowserOpen()) &&
                (mouseButton == 0 || mouseButton == 1))
            {
                // O clique pode chegar antes de um MouseMove no mesmo frame;
                // atualiza a posição física antes de testar se ele está fora.
                Menu::UsePhysicalSettingsCursor();
                // A captura não depende mais do timing de heldDownSecs:
                // qualquer evento ainda pressionado fora do botão encerra a
                // espera imediatamente, inclusive o clique no X de reset.
                if ((buttonEvent->IsDown() || buttonEvent->IsPressed()) &&
                    Menu::CancelQuickDrawHotkeyCaptureFromMouse(
                        static_cast<int>(mouseButton)))
                {
                    consumeButton(buttonEvent);
                    continue;
                }
                // Alguns drivers entregam o primeiro clique do editor já
                // com heldDownSecs acima de zero. IsDown é a borda confiável
                // aqui e permite cancelar a captura imediatamente.
                const bool firstDown = buttonEvent->IsDown();
                const bool released = !buttonEvent->IsPressed();
                const bool handled = (firstDown || released)
                    ? Menu::HandleQuickDrawEditorMouseButton(
                        static_cast<int>(mouseButton), !released)
                    : true;
                if (handled)
                {
                    consumeButton(buttonEvent);
                    continue;
                }
            }

            // ========================================================
            // QUICK DRAW - GAMEPLAY
            //
            // Enquanto o usuário ainda escolhe uma direção, os dois botões
            // iniciam/terminam o gesto e não chegam aos controles do jogo.
            // ========================================================
            if (mouseButton == 0 || mouseButton == 1)
            {
                const bool firstDown = buttonEvent->IsDown() &&
                    buttonEvent->GetRuntimeData().heldDownSecs <= 0.0f;
                const bool released = !buttonEvent->IsPressed();
                const bool topBottomHandled = firstDown &&
                    Menu::HandleTopBottomGameplayMouseButton(mouseButton, true);
                if (topBottomHandled)
                {
                    // O clique que confirmou o item vertical pertence ao
                    // Wheel e nunca deve acionar uma ação do Skyrim.
                    consumeButton(buttonEvent);
                    continue;
                }
                const bool quickDrawActive = Menu::IsQuickDrawGameplayActive();
                const bool handled = firstDown
                    ? Menu::BeginQuickDrawGameplayStroke(mouseButton, true)
                    : released
                        ? Menu::BeginQuickDrawGameplayStroke(mouseButton, false)
                        : quickDrawActive;
                if (handled)
                {
                    consumeButton(buttonEvent);
                    continue;
                }
            }
            
            // ========================================================
            // SETTINGS - CLIQUES DO MOUSE
            // ========================================================

            if (settingsOpen)
            {

                if (Menu::IsQuickDrawEditorOpen() &&
                    (mouseButton == 0 || mouseButton == 1))
                {
                    const bool firstDown = buttonEvent->IsDown() &&
                        buttonEvent->GetRuntimeData().heldDownSecs <= 0.0f;
                    const bool released = !buttonEvent->IsPressed();
                    const bool handled = (firstDown || released)
                        ? Menu::HandleQuickDrawEditorMouseButton(
                            static_cast<int>(mouseButton), !released)
                        : true;
                    if (handled)
                    {
                        consumeButton(buttonEvent);
                        continue;
                    }
                }

                // ====================================================
                // BOTÃO ESQUERDO - LOCK / DRAG
                // ====================================================

                if (mouseButton == 0)
                {
                    if (TrackEditor::IsOpen())
                    {
                        if (buttonEvent->IsDown() &&
                            buttonEvent->GetRuntimeData().heldDownSecs <= 0.0f)
                            TrackEditor::MouseDown(
                                Resolution::ToVirtual(ImGui::GetIO().MousePos), 0);
                        else if (!buttonEvent->IsPressed())
                            TrackEditor::MouseUp(
                                Resolution::ToVirtual(ImGui::GetIO().MousePos), 0);
                        consumeButton(buttonEvent);
                        continue;
                    }
                    bool consumeClick = g_settingsOwnsLeftClick;

                    if (buttonEvent->IsDown())
                    {
                        // Executa somente no primeiro pressionamento.
                        if (buttonEvent->GetRuntimeData().heldDownSecs <= 0.0f)
                        {
                            // ====================================================
                            // 1: BOTÃO DE FECHAR SETTINGS
                            // ====================================================

                            if (Menu::SettingsCloseButtonClick())
                            {
                                // Settings fechado.
                                // Não inicia drag nem aciona outros botões.
                                g_settingsOwnsLeftClick = true;
                            }
                            // ====================================================
                            // 2. BOTÕES DETECT WHEEL TOGGLE KEY
                            // ====================================================

                            else if (Menu::SettingsWheelKeyClick())
                            {
                                // Começou a capturar a nova tecla.
                                g_settingsOwnsLeftClick = true;
                            }

                            // ============================================================
                            // 3. Click do description gameplay
                            // ============================================================

                            else if (Menu::SettingsGameplayDescriptionClick())
                            {
                                // Alterna a descrição durante o gameplay.
                                g_settingsOwnsLeftClick = true;
                            }
                            // ====================================================
                            // 4. BOTÕES DE TRAVA DOS RADIAIS
                            // ====================================================

                            else if (Menu::SettingsLockClick())
                            {
                                // Clique processado.
                                g_settingsOwnsLeftClick = true;
                            }

                            // ====================================================
                            // 5. BOTÕES DE LIBERAÇÃO DO MOUSE
                            // ====================================================

                            else if (Menu::HandleMouseUnlockClick())
                            {
                                // Clique processado.
                                g_settingsOwnsLeftClick = true;
                            }

                            // ====================================================
                            // 6. DRAG NORMAL DOS ITENS
                            // ====================================================

                            else
                            {
                                g_settingsOwnsLeftClick = Menu::SettingsMouseDown();
                            }

                            consumeClick = g_settingsOwnsLeftClick;
                        }
                    }
                    // IsUp() requires a positive held duration in CommonLib.
                    // A very quick click can report value 0 with duration 0,
                    // so use the actual pressed state to always release a
                    // captured settings slider.
                    else if (!buttonEvent->IsPressed())
                    {
                        if (g_settingsOwnsLeftClick)
                        {
                            Menu::SettingsMouseUp();
                            g_settingsOwnsLeftClick = false;
                            consumeClick = true;
                        }
                    }

                    if (consumeClick)
                    {
                        // Consome somente cliques pertencentes ao Wheel.
                        buttonEvent->GetRuntimeData().value = 0.0f;
                        buttonEvent->GetRuntimeData().heldDownSecs = 0.0f;
                        buttonEvent->SetUserEvent("");
                        buttonEvent->SetIDCode(0xFF);
                    }

                    continue;
                }

                // ====================================================
                // BOTÃO DIREITO - DELETAR ITEM
                // ====================================================

                if (mouseButton == 1)
                {
                    if (TrackEditor::IsOpen())
                    {
                        if (buttonEvent->IsDown() &&
                            buttonEvent->GetRuntimeData().heldDownSecs <= 0.0f)
                            TrackEditor::MouseDown(
                                Resolution::ToVirtual(ImGui::GetIO().MousePos), 1);
                        else if (!buttonEvent->IsPressed())
                            TrackEditor::MouseUp(
                                Resolution::ToVirtual(ImGui::GetIO().MousePos), 1);
                        consumeButton(buttonEvent);
                        continue;
                    }
                    bool consumeClick = g_settingsOwnsRightClick;

                    if (buttonEvent->IsDown() &&
                        buttonEvent->GetRuntimeData().heldDownSecs <= 0.0f)
                    {
                        g_settingsOwnsRightClick = Menu::SettingsRightClick();
                        consumeClick = g_settingsOwnsRightClick;
                    }

                    if (!buttonEvent->IsPressed())
                    {
                        consumeClick = g_settingsOwnsRightClick;
                        g_settingsOwnsRightClick = false;
                    }

                    if (consumeClick)
                    {
                        // Consome somente o clique de remoção do Wheel.
                        buttonEvent->GetRuntimeData().value = 0.0f;
                        buttonEvent->GetRuntimeData().heldDownSecs = 0.0f;
                        buttonEvent->SetUserEvent("");
                        buttonEvent->SetIDCode(0xFF);
                    }

                    continue;
                }
            }
            
            // ========================================================
            // SETTINGS - SCROLL INDEPENDENTE
            // ========================================================

            // Enquanto uma hotkey está pressionada no modo HUD, a roda troca
            // somente a seleção temporária do próprio HUD. Consumimos o
            // notch aqui, antes de qualquer caminho do Skyrim ou dos radiais.
            if ((mouseButton == 8 || mouseButton == 9) &&
                buttonEvent->GetRuntimeData().value != 0.0f)
            {
                const int direction = (mouseButton == 8) ? 1 : -1;
                if (Menu::ScrollQuickDrawHotkeyHud(direction))
                {
                    consumeButton(buttonEvent);
                    continue;
                }
            }

            if (settingsOpen &&
                (mouseButton == 8 || mouseButton == 9))
            {
                if (buttonEvent->GetRuntimeData().value != 0.0f)
                {
                    const int direction =
                        (mouseButton == 8) ? 1 : -1;

                    if (TrackEditor::IsOpen())
                    {
                        TrackEditor::Scroll(direction);
                        consumeButton(buttonEvent);
                        continue;
                    }

                    if (Menu::ScrollQuickDrawEditorSettings(direction))
                    {
                        consumeButton(buttonEvent);
                        continue;
                    }

                    // A bolsa recebe o notch real do InputSink antes do
                    // painel INFORMATION e dos radiais. ImGui::MouseWheel
                    // não é alimentado neste fluxo.
                    if (!Menu::ScrollSettingsItemBag(direction) &&
                        !Menu::ScrollSettingsItemInfo(direction))
                    {
                        // Mouse fora do painel:
                        // continua rolando os radiais normalmente.

                        Menu::ScrollSettingsRadial(direction);
                    }
                }

                // Impede o Skyrim de processar o scroll.
                buttonEvent->GetRuntimeData().value = 0.0f;
                buttonEvent->GetRuntimeData().heldDownSecs = 0.0f;
                buttonEvent->SetUserEvent("");
                buttonEvent->SetIDCode(0xFF);

                continue;
            }

            // ========================================================
            // INPUT ORIGINAL
            // ========================================================

            if (mouseButton == 8 || mouseButton == 9)
            {
                if (g_showWindow)
                {
                    if (buttonEvent->GetRuntimeData().value != 0.0f)
                    {
                        // ============================================================
                        // SETTINGS - SCROLL NO CENTRO DO RADIAL
                        // ============================================================

                        if (g_radialMode == RadialMode::Gameplay &&
                            g_radialSide == RadialSide::None)
                        {
                            Menu::HandleSettingsScroll();
                        }
                        else
                        {
                            // Aqui permanece sua lógica original de scroll
                            // dos radiais Top, Bottom, Left e Right.
                        }
                        const float scrollDelta =
                            (mouseButton == 8) ? -1.0f : 1.0f;

                        // ====================================================
                        // TOP / BOTTOM
                        // ====================================================

                        if (g_radialSide == RadialSide::Top ||
                            g_radialSide == RadialSide::Bottom)
                        {
                            const int direction =
                                (mouseButton == 8) ? 1 : -1;

                            Menu::ScrollTopBottomRadial(direction);
                        }
                        else if (
                            g_radialSide == RadialSide::Left ||
                            g_radialSide == RadialSide::Right)
                        {
                            Menu::ScrollSideRadial(
                                scrollDelta > 0.0f ? -1 : 1
                            );
                        }

                    }

                    // ========================================================
                    // CONSOME O WHEEL
                    // ========================================================

                    buttonEvent->GetRuntimeData().value = 0.0f;
                    buttonEvent->GetRuntimeData().heldDownSecs = 0.0f;
                    buttonEvent->SetUserEvent("");
                    buttonEvent->SetIDCode(0xFF);
                }

                continue;
            }
        }
    }

    return RE::BSEventNotifyControl::kContinue;
}
