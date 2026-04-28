#pragma once

class VanillaBlurBlocker
{
public:
    static VanillaBlurBlocker& GetSingleton();

    void Start();
    void Stop();
    [[nodiscard]] bool IsActive() const;

private:
    bool active = false;
};
