#include "EventProcessor.h"

#include "ItemDisplay.h"
#include "MenuCamera.h"
#include "Settings.h"

namespace
{
    std::atomic_bool blurClearQueued = false;

    // the menu's blur taken away, given back if the one shown goes while the menu stays open (the mod turned off)
    std::uint32_t blurCleared = 0;

    // only while someone is shown, so a menu with the mod off, or one it couldn't start in (mid sit-down, the free or furniture camera), keeps its blur
    void ClearVanillaMenuBlur()
    {
        if (!MenuCamera::GetSingleton()->IsActive()) {
            return;
        }

        auto* blur = RE::UIBlurManager::GetSingleton();
        while (blur->blurCount > 0) {
            blur->DecrementBlurCount();
            ++blurCleared;
        }
    }

    void RestoreVanillaMenuBlur()
    {
        auto* blur = RE::UIBlurManager::GetSingleton();
        for (; blurCleared > 0; --blurCleared) {
            blur->IncrementBlurCount();
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
    if (opening) {
        MenuCamera::HookMenu(menuName);
        ItemDisplay::OnMenuOpen(menuName);
    }

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
        blurCleared = 0;
        MenuCamera::GetSingleton()->SetCacheFrozen(true);
        activeMenu = menuName;
        ApplyLiveSettings();
        return;
    }

    if (menuOpen && menuName == activeMenu) {
        menuOpen = false;
        blurCleared = 0;  // the closing menu takes its own blur away, so there's nothing to give back
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
        QueueVanillaMenuBlurClear();
    } else {
        MenuCamera::GetSingleton()->Stop();
        RestoreVanillaMenuBlur();
    }
}
