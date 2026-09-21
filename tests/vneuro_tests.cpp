#include "../vneuro.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
template <typename F> bool throws_invalid_argument(F&& f)
{
    try
    {
        std::forward<F>(f)();
    }
    catch (const std::invalid_argument&)
    {
        return true;
    }
    return false;
}

void configuration_validation()
{
    assert(throws_invalid_argument([]() { vstd::neuro<> network(std::vector<std::size_t>{}, 0.1); }));
    assert(throws_invalid_argument([]() { vstd::neuro<> network(std::vector<std::size_t>{2}, 0.1); }));
    assert(throws_invalid_argument([]() { vstd::neuro<> network(std::vector<std::size_t>{2, 0, 1}, 0.1); }));
    assert(throws_invalid_argument([]() { vstd::neuro<> network(std::vector<int>{2, -1, 1}, 0.1, 1.0, 0.1); }));
    assert(throws_invalid_argument([]() { vstd::neuro<> network({2, 1}, 0.0); }));
    assert(throws_invalid_argument([]() { vstd::neuro<> network({2, 1}, 0.1, 1.0); }));
    assert(throws_invalid_argument([]() { vstd::neuro<> network({2, 1}, 0.1, 0.0, 0.0); }));
}

void input_validation()
{
    vstd::neuro<> network({2, 3, 1}, 0.2, 0.0, 1.0, 7);
    assert(throws_invalid_argument([&]() { network.predict(std::vector<double>{1.0}); }));
    assert(throws_invalid_argument(
        [&]() { network.predict(std::vector<double>{std::numeric_limits<double>::quiet_NaN(), 1.0}); }));
    assert(throws_invalid_argument([&]() { network.add_training_sample({0.0, 1.0}, {0.0, 1.0}); }));
    assert(throws_invalid_argument([&]() { network.add_test_sample({0.0}, {1.0}); }));
}

void deterministic_copy_and_seed()
{
    vstd::neuro<> first({2, 3, 1}, 0.2, 0.1, 1.0, 99);
    vstd::neuro<> second({2, 3, 1}, 0.2, 0.1, 1.0, 99);
    const std::vector<double> input{0.25, 0.75};
    assert(first.predict(input) == second.predict(input));

    first.add_training_sample(input, {0.6});
    auto copy = first;
    first.train(5);
    copy.train(5);
    assert(first.predict(input) == copy.predict(input));
    assert(first.parameter_count() == copy.parameter_count());
}

void legacy_api_compatibility()
{
    vstd::neuro<> network(std::vector<int>{2, 3, 1}, 0.1, 1.0, 0.3, 123);
    network.add_teacher({0.0, 1.0}, {1.0});
    network.add_test({1.0, 0.0}, {1.0});
    network.teach(2);
    assert(network.training_sample_count() == 1);
    assert(network.test_sample_count() == 1);
    assert(std::isfinite(network.erms()));
    assert(std::isfinite(network.test()));
}

void empty_dataset_behavior()
{
    vstd::neuro<> network({2, 2, 1}, 0.2, 0.0, 1.0, 11);
    assert(network.training_error() == 0.0);
    assert(network.test_error() == 0.0);
    assert(network.train_until(0.01, 10) == 0);
    network.train(10);
}

void gradient_check()
{
    vstd::neuro<> network({2, 2, 1}, 0.2, 0.0, 1.0, 23);
    const double error = network.gradient_check(std::vector<double>{0.25, 0.75}, std::vector<double>{0.6});
    assert(error < 1e-5);
}

void training_reduces_error()
{
    vstd::neuro<> network({2, 3, 1}, 0.2, 0.0, 1.0, 17);
    network.add_training_sample({0.2, 0.8}, {0.9});
    const double before = network.training_error();
    network.train(100);
    const double after = network.training_error();
    assert(after < before);
}

void xor_training()
{
    vstd::neuro<> network({2, 4, 1}, 0.5, 0.1, 1.0, 42);
    network.add_training_sample({0.0, 0.0}, {0.0});
    network.add_training_sample({0.0, 1.0}, {1.0});
    network.add_training_sample({1.0, 0.0}, {1.0});
    network.add_training_sample({1.0, 1.0}, {0.0});
    network.train(20'000);

    assert(network.predict(std::vector<double>{0.0, 0.0})[0] < 0.1);
    assert(network.predict(std::vector<double>{0.0, 1.0})[0] > 0.9);
    assert(network.predict(std::vector<double>{1.0, 0.0})[0] > 0.9);
    assert(network.predict(std::vector<double>{1.0, 1.0})[0] < 0.1);
}
} // namespace

int main()
{
    configuration_validation();
    input_validation();
    deterministic_copy_and_seed();
    legacy_api_compatibility();
    empty_dataset_behavior();
    gradient_check();
    training_reduces_error();
    xor_training();
    return 0;
}
