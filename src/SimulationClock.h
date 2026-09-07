#pragma once

class SimulationClock
{
public:
    void update(float realSeconds) noexcept;

    void togglePaused() noexcept;
    void setPaused(bool paused) noexcept;
    [[nodiscard]] bool paused() const noexcept;

    void setSpeed(float multiplier) noexcept;
    [[nodiscard]] float speed() const noexcept;

    [[nodiscard]] double elapsedSimulationSeconds() const noexcept;
    [[nodiscard]] int day() const noexcept;
    [[nodiscard]] int hour() const noexcept;
    [[nodiscard]] int minute() const noexcept;

private:
    bool paused_ = false;
    float speed_ = 1.0f;
    double elapsedSimulationSeconds_ = 0.0;
};
