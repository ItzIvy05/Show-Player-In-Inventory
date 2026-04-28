#include "VanillaBlurBlocker.h"

VanillaBlurBlocker& VanillaBlurBlocker::GetSingleton()
{
    static VanillaBlurBlocker instance;
    return instance;
}

void VanillaBlurBlocker::Start()
{
    if (active) {
        return;
    }

    auto* blurManager = RE::UIBlurManager::GetSingleton();
    if (!blurManager) {
        logger::warn("[VanillaBlurBlocker] Could not find UI blur manager.");
        return;
    }

    blurManager->DecrementBlurCount();
    active = true;
    logger::debug("[VanillaBlurBlocker] Suppressed vanilla inventory blur.");
}

void VanillaBlurBlocker::Stop()
{
    active = false;
}

bool VanillaBlurBlocker::IsActive() const
{
    return active;
}
