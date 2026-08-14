#include "EventProcessor.h"

#include "MenuCamera.h"
#include "Settings.h"

SKSE::PluginHandle g_pluginHandle = SKSE::kInvalidPluginHandle;

namespace
{
    std::atomic_bool blurClearQueued = false;

    void ClearVanillaMenuBlur()
    {
        auto* blur = RE::UIBlurManager::GetSingleton();
        if (!blur) {
            return;
        }

        for (std::int32_t i = 0; i < 8 && blur->blurCount > 0; ++i) {
            blur->DecrementBlurCount();
        }

        blur->blurCount = 0;
    }

    void QueueVanillaMenuBlurClear()
    {
        ClearVanillaMenuBlur();

        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            return;
        }

        bool expected = false;
        if (!blurClearQueued.compare_exchange_strong(expected, true)) {
            return;
        }

        tasks->AddUITask([] {
            ClearVanillaMenuBlur();
            blurClearQueued.store(false);
        });
    }
}

EventProcessor& EventProcessor::GetSingleton()
{
    static EventProcessor instance;
    return instance;
}

RE::BSEventNotifyControl EventProcessor::ProcessEvent(
    const RE::MenuOpenCloseEvent* event,
    RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
    if (!event) {
        return RE::BSEventNotifyControl::kContinue;
    }

    if (menuOpen) {
        QueueVanillaMenuBlurClear();
    }

    if (event->opening) {
        if (!Settings::IsWatchedMenu(event->menuName)) {
            return RE::BSEventNotifyControl::kContinue;
        }

        menuOpen = true;
        activeMenu = event->menuName;
        logger::info("[EventProcessor] Watched menu opened.");
        ApplyLiveSettings();
        return RE::BSEventNotifyControl::kContinue;
    }

    if (menuOpen && event->menuName == activeMenu) {
        logger::info("[EventProcessor] Watched menu closed.");
        MenuCamera::GetSingleton().Stop();
        menuOpen = false;
        QueueVanillaMenuBlurClear();
    }

    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl EventProcessor::ProcessEvent(
    RE::InputEvent* const* event,
    RE::BSTEventSource<RE::InputEvent*>*)
{
    if (!event || !MenuCamera::GetSingleton().IsActive()) {
        rotating = false;
        return RE::BSEventNotifyControl::kContinue;
    }

    for (auto* current = *event; current; current = current->next) {
        if (const auto* button = current->AsButtonEvent()) {
            std::uint32_t code = button->GetIDCode();

            switch (button->device.get()) {
            case RE::INPUT_DEVICE::kMouse:
                code += SKSE::InputMap::kMacro_MouseButtonOffset;
                break;

            case RE::INPUT_DEVICE::kGamepad:
                code = SKSE::InputMap::GamepadMaskToKeycode(code);
                break;

            default:
                break;
            }

            if (code == Settings::rotateKey) {
                rotating = button->Value() > 0.0f;
            }

            continue;
        }

        if (const auto* move = current->AsMouseMoveEvent(); move && rotating) {
            MenuCamera::GetSingleton().Rotate(static_cast<float>(move->mouseInputX));
            continue;
        }

        if (const auto* stick = current->AsThumbstickEvent(); stick && rotating && stick->IsRight()) {
            MenuCamera::GetSingleton().Rotate(stick->xValue * 5.0f);
        }
    }

    return RE::BSEventNotifyControl::kContinue;
}

void EventProcessor::ApplyLiveSettings()
{
    if (!menuOpen) {
        return;
    }

    if (!Settings::enabled) {
        MenuCamera::GetSingleton().Stop();
        QueueVanillaMenuBlurClear();
        return;
    }

    if (MenuCamera::GetSingleton().IsActive()) {
        MenuCamera::GetSingleton().ApplySettings();
    } else {
        MenuCamera::GetSingleton().Start();
    }

    QueueVanillaMenuBlurClear();
}
