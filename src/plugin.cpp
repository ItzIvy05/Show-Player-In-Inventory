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
            APIs::RequestAPIs();
            break;

        case SKSE::MessagingInterface::kDataLoaded:
            Settings::Load();
            SettingsUI::Register();
            MenuCamera::InstallHook();
            RE::UI::GetSingleton()->AddEventSink<RE::MenuOpenCloseEvent>(EventProcessor::GetSingleton());
            RE::BSInputDeviceManager::GetSingleton()->AddEventSink<RE::InputEvent*>(EventProcessor::GetSingleton());
            logger::info("[IvyShowPlayerInMenus] Registered menu and input watchers.");
            break;

        default:
            break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    SKSE::Init(skse);
    Settings::ApplyLogLevel();

    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);

    logger::info("[IvyShowPlayerInMenus] Plugin loaded.");

    return true;
}
