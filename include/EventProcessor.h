#pragma once

class EventProcessor final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
    static EventProcessor& GetSingleton();

    RE::BSEventNotifyControl ProcessEvent(
        const RE::MenuOpenCloseEvent* event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>* source) override;

    void ApplyLiveSettings();
    [[nodiscard]] bool IsInventoryOpen() const;

private:
    bool inventoryOpen = false;
};

extern SKSE::PluginHandle g_pluginHandle;
