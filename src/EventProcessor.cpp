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

    if (inventoryOpen) {
        QueueVanillaMenuBlurClear();
    }

    if (!Settings::IsInventoryMenu(event->menuName)) {
        return RE::BSEventNotifyControl::kContinue;
    }

    if (event->opening) {
        inventoryOpen = true;
        logger::info("[EventProcessor] Inventory Menu opened.");
        QueueVanillaMenuBlurClear();
        ApplyLiveSettings();
        QueueVanillaMenuBlurClear();
        return RE::BSEventNotifyControl::kContinue;
    }

    if (inventoryOpen) {
        logger::info("[EventProcessor] Inventory Menu closed.");
        MenuCamera::GetSingleton().Stop();
        inventoryOpen = false;
        QueueVanillaMenuBlurClear();
    }

    return RE::BSEventNotifyControl::kContinue;
}

void EventProcessor::ApplyLiveSettings()
{
    if (!inventoryOpen) {
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

bool EventProcessor::IsInventoryOpen() const
{
    return inventoryOpen;
}
