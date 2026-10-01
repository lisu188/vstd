#include "vutil.h"

#include <array>
#include <climits>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

class RandomStateGuard
{
    std::mt19937_64 saved = vstd::rng();

  public:
    ~RandomStateGuard()
    {
        vstd::rng() = saved;
    }
};

void checkRanges()
{
    vstd::rng().seed(20261001);
    for (int sample = 0; sample < 1000; ++sample)
    {
        require(vstd::rand(7, 7) == 7, "positive singleton");
        require(vstd::rand(-7, -7) == -7, "negative singleton");
        require(vstd::rand(0) == 0, "zero singleton");
        const int negative = vstd::rand(-12, -3);
        require(negative >= -12 && negative <= -3, "negative interval");
        const int crossed = vstd::rand(-2, 3);
        require(crossed >= -2 && crossed <= 3, "interval crossing zero");
        const int reversed = vstd::rand(3, -2);
        require(reversed >= -2 && reversed <= 3, "reversed interval");
        const int positive_bound = vstd::rand(4);
        require(positive_bound >= 0 && positive_bound <= 4, "inclusive one-argument interval");
        const int negative_bound = vstd::rand(-4);
        require(negative_bound >= -4 && negative_bound <= 0, "negative one-argument interval");
        const int mixed_bounds = vstd::rand(-3, std::size_t{3});
        require(mixed_bounds >= -3 && mixed_bounds <= 3, "mixed signed and unsigned bounds");
        const int wide = vstd::rand(INT_MIN, INT_MAX);
        require(wide >= INT_MIN && wide <= INT_MAX, "full int interval");
    }

    vstd::rng().seed(4261);
    const auto initial = vstd::rng();
    const int ordered = vstd::rand(-12, 8);
    vstd::rng() = initial;
    require(vstd::rand(8, -12) == ordered, "reversing bounds preserves the normalized sample");
}

template <std::size_t Size> void checkDistribution(int lower, int samples, unsigned seed)
{
    std::array<int, Size> counts{};
    vstd::rng().seed(seed);
    for (int sample = 0; sample < samples; ++sample)
    {
        const int value = vstd::rand(lower, lower + static_cast<int>(Size) - 1);
        require(value >= lower && value < lower + static_cast<int>(Size), "distribution sample range");
        ++counts[static_cast<std::size_t>(value - lower)];
    }
    const int expected = samples / static_cast<int>(Size);
    for (std::size_t bucket = 0; bucket < Size; ++bucket)
    {
        require(counts[bucket] >= expected * 8 / 10 && counts[bucket] <= expected * 12 / 10,
                "biased distribution bucket " + std::to_string(bucket) + ": " + std::to_string(counts[bucket]));
    }
}

void checkRealSamplerUnchanged()
{
    vstd::rng().seed(80924);
    auto expected_engine = vstd::rng();
    for (int sample = 0; sample < 100; ++sample)
    {
        const double expected = vstd::unif()(expected_engine);
        const double actual = vstd::rand();
        require(actual == expected, "real sampler still uses the existing distribution");
        require(actual >= -0.5 && actual <= 0.5, "real sampler range");
    }
}
} // namespace

int main()
{
    RandomStateGuard guard;
    try
    {
        checkDistribution<4>(0, 64000, 401);
        checkDistribution<10>(1, 100000, 1010);
        checkRanges();
        checkRealSamplerUnchanged();
        std::cout << "Inclusive integer RNG range and distribution checks passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
