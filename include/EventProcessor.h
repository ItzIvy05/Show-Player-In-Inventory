#pragma once

class EventProcessor final :
    public RE::BSTEventSink<RE::MenuOpenCloseEvent>,
    public RE::BSTEventSink<RE::InputEvent*>
{
public:
    static EventProcessor& GetSingleton();

    RE::BSEventNotifyControl ProcessEvent(
        const RE::MenuOpenCloseEvent* event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>* source) override;

    RE::BSEventNotifyControl ProcessEvent(
        RE::InputEvent* const* event,
        RE::BSTEventSource<RE::InputEvent*>* source) override;

    void ApplyLiveSettings();

private:
    bool menuOpen = false;
    bool rotating = false;
};

extern SKSE::PluginHandle g_pluginHandle;
