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

        while (blur->blurCount > 0) {
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

    if (!Settings::IsWatchedMenu(event->menuName)) {
        return RE::BSEventNotifyControl::kContinue;
    }

    if (event->opening) {
        menuOpen = true;
        logger::info("[EventProcessor] Watched menu opened.");
        QueueVanillaMenuBlurClear();
        ApplyLiveSettings();
        QueueVanillaMenuBlurClear();
        return RE::BSEventNotifyControl::kContinue;
    }

    if (menuOpen) {
        logger::info("[EventProcessor] Watched menu closed.");
        MenuCamera::GetSingleton().Stop();
        menuOpen = false;
        QueueVanillaMenuBlurClear();
    }

    return RE::BSEventNotifyControl::kContinue;
}

void EventProcessor::ApplyLiveSettings()
{
    if (!menuOpen) {
        return;
    }

    QueueVanillaMenuBlurClear();

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
