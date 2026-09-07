#include "SimulationClock.h"

#include <algorithm>
#include <cmath>

namespace
{
    // One real second advances one in-game minute at 1x.
    constexpr double BaseSimulationRate = 60.0;
}

void SimulationClock::update(float realSeconds) noexcept
{
    if (paused_ || realSeconds <= 0.0f)
    {
        return;
    }

    elapsedSimulationSeconds_ +=
        static_cast<double>(realSeconds) *
        static_cast<double>(speed_) *
        BaseSimulationRate;
}

void SimulationClock::togglePaused() noexcept
{
    paused_ = !paused_;
}

void SimulationClock::setPaused(bool paused) noexcept
{
    paused_ = paused;
}

bool SimulationClock::paused() const noexcept
{
    return paused_;
}

void SimulationClock::setSpeed(float multiplier) noexcept
{
    speed_ = std::clamp(multiplier, 1.0f, 64.0f);
}

float SimulationClock::speed() const noexcept
{
    return speed_;
}

double SimulationClock::elapsedSimulationSeconds() const noexcept
{
    return elapsedSimulationSeconds_;
}

int SimulationClock::day() const noexcept
{
    return static_cast<int>(elapsedSimulationSeconds_ / 86400.0) + 1;
}

int SimulationClock::hour() const noexcept
{
    const auto wholeSeconds = static_cast<long long>(std::floor(elapsedSimulationSeconds_));
    return static_cast<int>((wholeSeconds / 3600LL) % 24LL);
}

int SimulationClock::minute() const noexcept
{
    const auto wholeSeconds = static_cast<long long>(std::floor(elapsedSimulationSeconds_));
    return static_cast<int>((wholeSeconds / 60LL) % 60LL);
}
