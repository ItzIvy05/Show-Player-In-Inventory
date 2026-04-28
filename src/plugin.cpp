#include "logger.h"

#include "APIManager.h"
#include "EventProcessor.h"
#include "MenuCamera.h"
#include "Settings.h"
#include "SettingsUI.h"

namespace
{
    void OnMessage(SKSE::MessagingInterface::Message* message)
    {
        switch (message->type) {
        case SKSE::MessagingInterface::kPostLoad:
            APIs::RegisterCallbacks();
            break;

        case SKSE::MessagingInterface::kPostPostLoad:
        case SKSE::MessagingInterface::kNewGame:
        case SKSE::MessagingInterface::kPostLoadGame:
            APIs::RequestAPIs();
            break;

        case SKSE::MessagingInterface::kDataLoaded:
            Settings::Load();
            APIs::RequestAPIs();

            if (auto* ui = RE::UI::GetSingleton()) {
                ui->AddEventSink<RE::MenuOpenCloseEvent>(&EventProcessor::GetSingleton());
                logger::info("[IvyShowPlayerInMenus] Registered inventory menu watcher.");
            } else {
                logger::warn("[IvyShowPlayerInMenus] Could not register menu watcher.");
            }

            break;

        default:
            break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    SetupLog();
    REL::Module::reset();

    SKSE::Init(skse);
    g_pluginHandle = skse->GetPluginHandle();

    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
    SettingsUI::Register();

    logger::info("[IvyShowPlayerInMenus] Plugin loaded.");

    return true;
}
