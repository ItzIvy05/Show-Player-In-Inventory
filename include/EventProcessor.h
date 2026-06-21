#pragma once

class EventProcessor final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
    static EventProcessor& GetSingleton();

    RE::BSEventNotifyControl ProcessEvent(
        const RE::MenuOpenCloseEvent* event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>* source) override;

    void ApplyLiveSettings();

private:
    bool menuOpen = false;
};

extern SKSE::PluginHandle g_pluginHandle;
