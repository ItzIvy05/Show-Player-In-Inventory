#pragma once

class EventProcessor final : public REX::Singleton<EventProcessor>, public RE::BSTEventSink<RE::MenuOpenCloseEvent>, public RE::BSTEventSink<RE::InputEvent*>
{
public:
    RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event, RE::BSTEventSource<RE::MenuOpenCloseEvent>* source) override;
    RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* event, RE::BSTEventSource<RE::InputEvent*>* source) override;

    void ApplyLiveSettings();

    [[nodiscard]] static std::uint32_t GetKeyCode(const RE::ButtonEvent* button);

private:
    RE::BSFixedString activeMenu;
    bool menuOpen = false;
    bool tweenOpen = false;
    bool rotating = false;
};
