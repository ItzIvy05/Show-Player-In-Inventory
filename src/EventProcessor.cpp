#include "EventProcessor.h"

#include "MenuCamera.h"
#include "Settings.h"

namespace
{
    std::atomic_bool blurClearQueued = false;

    void ClearVanillaMenuBlur()
    {
        auto* blur = RE::UIBlurManager::GetSingleton();
        while (blur->blurCount > 0) {
            blur->DecrementBlurCount();
        }
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

void EventProcessor::Register()
{
    listener = FUCK::MenuEventListener([this](const char* menuName, bool opening) {
        if (menuName) {
            OnMenu(menuName, opening);
        }
    });
}

void EventProcessor::OnMenu(std::string_view menuName, bool opening)
{
    if (menuName == RE::TweenMenu::MENU_NAME) {
        tweenOpen = opening;
        MenuCamera::GetSingleton()->SetCacheFrozen(tweenOpen || menuOpen);
    }

    if (menuOpen) {
        QueueVanillaMenuBlurClear();
    }

    if (opening) {
        if (!Settings::IsWatchedMenu(menuName)) {
            return;
        }

        menuOpen = true;
        MenuCamera::GetSingleton()->SetCacheFrozen(true);
        activeMenu = menuName;
        logger::info("[EventProcessor] Watched menu opened.");
        ApplyLiveSettings();
        return;
    }

    if (menuOpen && menuName == activeMenu) {
        logger::info("[EventProcessor] Watched menu closed.");
        menuOpen = false;
        QueueStop();
        QueueVanillaMenuBlurClear();
    }
}

// a frame or two late, so switching straight to another watched menu (inventory to magic) keeps the camera rather than snapping back and setting up again
void EventProcessor::QueueStop()
{
    const auto generation = ++stopGeneration;
    SKSE::GetTaskInterface()->AddUITask([this, generation] {
        SKSE::GetTaskInterface()->AddUITask([this, generation] {
            if (generation != stopGeneration || menuOpen) {
                return;
            }

            MenuCamera::GetSingleton()->Stop();
            MenuCamera::GetSingleton()->SetCacheFrozen(tweenOpen);
        });
    });
}

void EventProcessor::ApplyLiveSettings()
{
    if (!menuOpen) {
        return;
    }

    if (Settings::values.enabled) {
        MenuCamera::GetSingleton()->Start(activeMenu);
    } else {
        MenuCamera::GetSingleton()->Stop();
    }

    QueueVanillaMenuBlurClear();
}
