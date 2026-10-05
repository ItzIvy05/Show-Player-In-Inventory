#include "EventProcessor.h"

#include "MenuCamera.h"
#include "Settings.h"

namespace
{
    std::atomic_bool blurClearQueued = false;

    void ClearVanillaMenuBlur()
    {
        auto* blur = RE::UIBlurManager::GetSingleton();
        for (std::int32_t i = 0; i < 8 && blur->blurCount > 0; ++i) {
            blur->DecrementBlurCount();
        }

        blur->blurCount = 0;
    }

    void QueueVanillaMenuBlurClear()
    {
        ClearVanillaMenuBlur();

        bool expected = false;
        if (!blurClearQueued.compare_exchange_strong(expected, true)) {
            return;
        }

        SKSE::GetTaskInterface()->AddUITask([] {
            ClearVanillaMenuBlur();
            blurClearQueued.store(false);
        });
    }
}

RE::BSEventNotifyControl EventProcessor::ProcessEvent(const RE::MenuOpenCloseEvent* event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
    if (!event) {
        return RE::BSEventNotifyControl::kContinue;
    }

    if (event->menuName == RE::TweenMenu::MENU_NAME) {
        tweenOpen = event->opening;
        MenuCamera::GetSingleton()->SetCacheFrozen(tweenOpen || menuOpen);
    }

    if (menuOpen) {
        QueueVanillaMenuBlurClear();
    }

    if (event->opening) {
        if (!Settings::IsWatchedMenu(event->menuName)) {
            return RE::BSEventNotifyControl::kContinue;
        }

        menuOpen = true;
        MenuCamera::GetSingleton()->SetCacheFrozen(true);
        activeMenu = event->menuName;
        logger::info("[EventProcessor] Watched menu opened.");
        ApplyLiveSettings();
        return RE::BSEventNotifyControl::kContinue;
    }

    if (menuOpen && event->menuName == activeMenu) {
        logger::info("[EventProcessor] Watched menu closed.");
        MenuCamera::GetSingleton()->Stop();
        menuOpen = false;
        MenuCamera::GetSingleton()->SetCacheFrozen(tweenOpen);
        QueueVanillaMenuBlurClear();
    }

    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl EventProcessor::ProcessEvent(RE::InputEvent* const* event, RE::BSTEventSource<RE::InputEvent*>*)
{
    if (!event || !MenuCamera::GetSingleton()->IsActive()) {
        rotating = false;
        return RE::BSEventNotifyControl::kContinue;
    }

    for (auto* current = *event; current; current = current->next) {
        if (const auto* button = current->AsButtonEvent()) {
            if (GetKeyCode(button) == Settings::rotateKey.GetValue()) {
                rotating = button->IsPressed();
            }

            continue;
        }

        if (const auto* move = current->AsMouseMoveEvent(); move && rotating) {
            MenuCamera::GetSingleton()->Rotate(static_cast<float>(move->mouseInputX));
            continue;
        }

        if (const auto* stick = current->AsThumbstickEvent(); stick && rotating && stick->IsRight()) {
            MenuCamera::GetSingleton()->Rotate(stick->xValue * 5.0f);
        }
    }

    return RE::BSEventNotifyControl::kContinue;
}

std::uint32_t EventProcessor::GetKeyCode(const RE::ButtonEvent* button)
{
    switch (button->GetDevice()) {
    case RE::INPUT_DEVICE::kMouse:
        return SKSE::InputMap::kMacro_MouseButtonOffset + button->GetIDCode();

    case RE::INPUT_DEVICE::kGamepad:
        return SKSE::InputMap::GamepadMaskToKeycode(button->GetIDCode());

    default:
        return button->GetIDCode();
    }
}

void EventProcessor::ApplyLiveSettings()
{
    if (!menuOpen) {
        return;
    }

    if (!Settings::enabled.GetValue()) {
        MenuCamera::GetSingleton()->Stop();
        QueueVanillaMenuBlurClear();
        return;
    }

    if (MenuCamera::GetSingleton()->IsActive()) {
        MenuCamera::GetSingleton()->ApplySettings();
    } else {
        MenuCamera::GetSingleton()->Start();
    }

    QueueVanillaMenuBlurClear();
}
