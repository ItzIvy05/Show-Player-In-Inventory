#include "EventProcessor.h"
#include "MenuCamera.h"
#include "Settings.h"
#include "SettingsUI.h"
#include "ViewSwitchFix.h"

namespace
{
    void OnMessage(SKSE::MessagingInterface::Message* message)
    {
        switch (message->type) {
        case SKSE::MessagingInterface::kDataLoaded:
            // FUCK first, as the settings and translations come through it
            SettingsUI::Register();
            Settings::Load();
            MenuCamera::InstallHooks();
            ViewSwitchFix::Install();
            EventProcessor::GetSingleton()->Register();
            logger::info("[Show Player In Inventory] Registered menu watcher.");
            break;

        case SKSE::MessagingInterface::kNewGame:
        case SKSE::MessagingInterface::kPostLoadGame:
            ViewSwitchFix::Reset();
            break;

        default:
            break;
        }
    }
}

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* skse)
{
    SKSE::Init(skse);

    // only a few lines a menu, so it's always on
    spdlog::set_level(spdlog::level::info);
    spdlog::flush_on(spdlog::level::info);

    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);

    logger::info("[Show Player In Inventory] Plugin loaded.");

    return true;
}
