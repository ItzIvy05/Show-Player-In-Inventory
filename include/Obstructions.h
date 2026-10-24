#pragma once

// things standing between the camera and the one shown are hidden while they're shown, and brought back after.
// only their drawing is switched off (the app cull flag), so nothing about them changes in the world or the save
namespace Obstructions
{
    // each frame while someone is shown, after the camera's placed. checks a few times a second
    void Update(float interval, RE::Actor* subject);

    // brings back everything hidden here. safe to call any time
    void RestoreAll();
}
