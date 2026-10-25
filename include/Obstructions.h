#pragma once

// things standing between the camera and the one shown are hidden while they're shown, and brought back after.
// only their drawing is switched off (the app cull flag), so nothing about them changes in the world or the save
namespace Obstructions
{
    // each frame while someone is shown, after the camera's placed. checks a few times a second. shown is everyone in view with them
    // (their horse, anyone they hold), who always stays. upperBody when they're framed from the chest up, so only what's in front of that counts
    void Update(float interval, RE::Actor* subject, const std::vector<RE::Actor*>& shown, bool upperBody);

    // brings back everything hidden here. safe to call any time
    void RestoreAll();
}
