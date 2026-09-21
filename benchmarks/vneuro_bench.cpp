#include "../vneuro.h"

#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace
{
using clock_type = std::chrono::steady_clock;

struct case_definition
{
    std::string name;
    std::vector<std::size_t> layers;
    std::size_t predictions;
    std::size_t training_epochs;
};

void run_case(const case_definition& definition)
{
    vstd::neuro<> network(definition.layers, 0.05, 0.1, 1.0, 12345);
    std::vector<double> input(definition.layers.front(), 0.25);
    std::vector<double> expected(definition.layers.back(), 0.75);
    network.add_training_sample(input, expected);

    const auto prediction_start = clock_type::now();
    for (std::size_t index = 0; index < definition.predictions; ++index)
    {
        const auto output = network.predict(input);
        if (output.empty())
        {
            return;
        }
    }
    const auto prediction_end = clock_type::now();

    const auto training_start = clock_type::now();
    network.train(definition.training_epochs);
    const auto training_end = clock_type::now();

    const double prediction_seconds = std::chrono::duration<double>(prediction_end - prediction_start).count();
    const double training_seconds = std::chrono::duration<double>(training_end - training_start).count();
    const double predictions_per_second = static_cast<double>(definition.predictions) / prediction_seconds;
    const double epochs_per_second = static_cast<double>(definition.training_epochs) / training_seconds;

    std::cout << std::left << std::setw(18) << definition.name << " params=" << std::setw(10)
              << network.parameter_count() << " predict/s=" << std::setw(14) << std::fixed << std::setprecision(0)
              << predictions_per_second << " train epochs/s=" << epochs_per_second << '\n';
}
} // namespace

int main()
{
    const std::vector<case_definition> cases{
        {"2-4-1", {2, 4, 1}, 200'000, 50'000},
        {"64-64-32", {64, 64, 32}, 20'000, 2'000},
        {"128-256-128-32", {128, 256, 128, 32}, 2'000, 200},
        {"784-128-64-10", {784, 128, 64, 10}, 1'000, 100},
    };

    for (const auto& definition : cases)
    {
        run_case(definition);
    }
    return 0;
}
