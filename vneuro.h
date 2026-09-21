/*
 * MIT License
 *
 * Copyright (c) 2019 Andrzej Lis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <numeric>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace vstd
{
template <typename T = void> class neuro
{
  public:
    using value_type = double;
    static constexpr std::uint64_t default_seed = 0x9E3779B97F4A7C15ULL;

    struct sample
    {
        std::vector<double> input;
        std::vector<double> expected;
    };

    explicit neuro(std::vector<std::size_t> layers, double learning_rate, double momentum = 0.0,
                   double sigmoid_beta = 1.0, std::uint64_t seed = default_seed)
        : layers_(std::move(layers)), learning_rate_(learning_rate), momentum_(momentum), sigmoid_beta_(sigmoid_beta),
          rng_(seed)
    {
        validate_configuration();
        initialize_parameters();
    }

    neuro(std::initializer_list<std::size_t> layers, double learning_rate, double momentum = 0.0,
          double sigmoid_beta = 1.0, std::uint64_t seed = default_seed)
        : neuro(std::vector<std::size_t>(layers), learning_rate, momentum, sigmoid_beta, seed)
    {
    }

    neuro(std::vector<int> layers, double alfa, double beta, double eta, std::uint64_t seed = default_seed)
        : neuro(convert_layers(layers), eta, alfa, beta, seed)
    {
    }

    std::vector<double> predict(std::span<const double> input) const
    {
        validate_input(input);
        auto activations = forward(input);
        return activations.back();
    }

    std::vector<double> predict(const std::vector<double>& input) const
    {
        return predict(std::span<const double>(input));
    }

    void add_training_sample(std::vector<double> input, std::vector<double> expected)
    {
        validate_sample(input, expected);
        training_.push_back({std::move(input), std::move(expected)});
        training_order_.push_back(training_.size() - 1);
    }

    void add_test_sample(std::vector<double> input, std::vector<double> expected)
    {
        validate_sample(input, expected);
        tests_.push_back({std::move(input), std::move(expected)});
    }

    void add_teacher(std::vector<double> input, std::vector<double> expected)
    {
        add_training_sample(std::move(input), std::move(expected));
    }

    void add_test(std::vector<double> input, std::vector<double> expected)
    {
        add_test_sample(std::move(input), std::move(expected));
    }

    void train(std::size_t epochs)
    {
        if (training_.empty() || epochs == 0)
        {
            return;
        }

        for (std::size_t epoch = 0; epoch < epochs; ++epoch)
        {
            std::shuffle(training_order_.begin(), training_order_.end(), rng_);
            for (const auto index : training_order_)
            {
                const auto& gradients = compute_gradients(training_[index].input, training_[index].expected);
                apply_gradients(gradients);
            }
        }
    }

    std::size_t train_until(double target_error, std::size_t max_epochs)
    {
        validate_target_error(target_error);
        std::size_t epochs = 0;
        while (epochs < max_epochs && training_error() > target_error)
        {
            train(1);
            ++epochs;
        }
        return epochs;
    }

    void teach(int iterations)
    {
        if (iterations < 0)
        {
            throw std::invalid_argument("iterations must be non-negative");
        }
        train(static_cast<std::size_t>(iterations));
    }

    int teach(double target_error, int step = 1)
    {
        if (step <= 0)
        {
            throw std::invalid_argument("step must be positive");
        }
        validate_target_error(target_error);
        constexpr std::size_t max_legacy_epochs = 1'000'000;
        std::size_t epochs = 0;
        while (epochs < max_legacy_epochs && training_error() > target_error)
        {
            const auto batch = std::min<std::size_t>(static_cast<std::size_t>(step), max_legacy_epochs - epochs);
            train(batch);
            epochs += batch;
        }
        if (training_error() > target_error)
        {
            throw std::runtime_error("target error was not reached within the legacy training limit");
        }
        return static_cast<int>(epochs);
    }

    double training_error() const
    {
        return dataset_rmse(training_);
    }

    double test_error() const
    {
        return dataset_rmse(tests_);
    }

    double erms() const
    {
        return training_error();
    }

    double test() const
    {
        return test_error();
    }

    double gradient_check(std::span<const double> input, std::span<const double> expected, double epsilon = 1e-6)
    {
        validate_sample(input, expected);
        if (!std::isfinite(epsilon) || epsilon <= 0.0)
        {
            throw std::invalid_argument("epsilon must be finite and positive");
        }

        const auto& analytical = compute_gradients(input, expected);
        double max_relative_error = 0.0;

        for (std::size_t layer = 0; layer < weights_.size(); ++layer)
        {
            for (std::size_t index = 0; index < weights_[layer].data.size(); ++index)
            {
                const double original = weights_[layer].data[index];
                weights_[layer].data[index] = original + epsilon;
                const double plus = sample_loss(input, expected);
                weights_[layer].data[index] = original - epsilon;
                const double minus = sample_loss(input, expected);
                weights_[layer].data[index] = original;
                const double numerical = (plus - minus) / (2.0 * epsilon);
                max_relative_error =
                    std::max(max_relative_error, relative_error(analytical.weights[layer].data[index], numerical));
            }

            for (std::size_t index = 0; index < biases_[layer].size(); ++index)
            {
                const double original = biases_[layer][index];
                biases_[layer][index] = original + epsilon;
                const double plus = sample_loss(input, expected);
                biases_[layer][index] = original - epsilon;
                const double minus = sample_loss(input, expected);
                biases_[layer][index] = original;
                const double numerical = (plus - minus) / (2.0 * epsilon);
                max_relative_error =
                    std::max(max_relative_error, relative_error(analytical.biases[layer][index], numerical));
            }
        }

        return max_relative_error;
    }

    double gradient_check(const std::vector<double>& input, const std::vector<double>& expected, double epsilon = 1e-6)
    {
        return gradient_check(std::span<const double>(input), std::span<const double>(expected), epsilon);
    }

    const std::vector<std::size_t>& layer_sizes() const noexcept
    {
        return layers_;
    }

    std::size_t parameter_count() const noexcept
    {
        std::size_t count = 0;
        for (std::size_t layer = 0; layer < weights_.size(); ++layer)
        {
            count += weights_[layer].data.size() + biases_[layer].size();
        }
        return count;
    }

    std::size_t training_sample_count() const noexcept
    {
        return training_.size();
    }

    std::size_t test_sample_count() const noexcept
    {
        return tests_.size();
    }

  private:
    struct matrix
    {
        std::size_t rows = 0;
        std::size_t cols = 0;
        std::vector<double> data;

        matrix() = default;
        matrix(std::size_t rows, std::size_t cols) : rows(rows), cols(cols), data(rows * cols) {}

        double& operator()(std::size_t row, std::size_t col)
        {
            return data[row * cols + col];
        }

        double operator()(std::size_t row, std::size_t col) const
        {
            return data[row * cols + col];
        }
    };

    struct gradients
    {
        std::vector<matrix> weights;
        std::vector<std::vector<double>> biases;
    };

    std::vector<std::size_t> layers_;
    double learning_rate_;
    double momentum_;
    double sigmoid_beta_;
    std::mt19937_64 rng_;
    std::vector<matrix> weights_;
    std::vector<matrix> velocity_weights_;
    std::vector<std::vector<double>> biases_;
    std::vector<std::vector<double>> velocity_biases_;
    std::vector<sample> training_;
    std::vector<sample> tests_;
    std::vector<std::size_t> training_order_;
    std::vector<std::vector<double>> activations_;
    std::vector<std::vector<double>> deltas_;
    gradients gradient_buffer_;

    static std::vector<std::size_t> convert_layers(const std::vector<int>& layers)
    {
        std::vector<std::size_t> converted;
        converted.reserve(layers.size());
        for (const auto layer : layers)
        {
            if (layer <= 0)
            {
                throw std::invalid_argument("layer sizes must be positive");
            }
            converted.push_back(static_cast<std::size_t>(layer));
        }
        return converted;
    }

    void validate_configuration() const
    {
        if (layers_.size() < 2)
        {
            throw std::invalid_argument("a neural network requires at least input and output layers");
        }
        if (std::any_of(layers_.begin(), layers_.end(), [](std::size_t size) { return size == 0; }))
        {
            throw std::invalid_argument("layer sizes must be positive");
        }
        if (!std::isfinite(learning_rate_) || learning_rate_ <= 0.0)
        {
            throw std::invalid_argument("learning rate must be finite and positive");
        }
        if (!std::isfinite(momentum_) || momentum_ < 0.0 || momentum_ >= 1.0)
        {
            throw std::invalid_argument("momentum must be finite and in [0, 1)");
        }
        if (!std::isfinite(sigmoid_beta_) || sigmoid_beta_ <= 0.0)
        {
            throw std::invalid_argument("sigmoid beta must be finite and positive");
        }
    }

    void initialize_parameters()
    {
        weights_.reserve(layers_.size() - 1);
        velocity_weights_.reserve(layers_.size() - 1);
        biases_.reserve(layers_.size() - 1);
        velocity_biases_.reserve(layers_.size() - 1);

        for (std::size_t layer = 0; layer + 1 < layers_.size(); ++layer)
        {
            const auto fan_in = layers_[layer];
            const auto fan_out = layers_[layer + 1];
            const double limit = std::sqrt(6.0 / static_cast<double>(fan_in + fan_out));
            std::uniform_real_distribution<double> distribution(-limit, limit);

            matrix weights(fan_out, fan_in);
            for (auto& weight : weights.data)
            {
                weight = distribution(rng_);
            }
            weights_.push_back(std::move(weights));
            velocity_weights_.emplace_back(fan_out, fan_in);
            biases_.emplace_back(fan_out, 0.0);
            velocity_biases_.emplace_back(fan_out, 0.0);
        }

        activations_.resize(layers_.size());
        for (std::size_t layer = 0; layer < layers_.size(); ++layer)
        {
            activations_[layer].resize(layers_[layer]);
        }

        deltas_.resize(weights_.size());
        gradient_buffer_.weights.reserve(weights_.size());
        gradient_buffer_.biases.reserve(weights_.size());
        for (std::size_t layer = 0; layer < weights_.size(); ++layer)
        {
            deltas_[layer].resize(layers_[layer + 1]);
            gradient_buffer_.weights.emplace_back(weights_[layer].rows, weights_[layer].cols);
            gradient_buffer_.biases.emplace_back(layers_[layer + 1]);
        }
    }

    double sigmoid(double value) const
    {
        const double scaled = sigmoid_beta_ * value;
        if (scaled >= 0.0)
        {
            const double z = std::exp(-scaled);
            return 1.0 / (1.0 + z);
        }
        const double z = std::exp(scaled);
        return z / (1.0 + z);
    }

    double sigmoid_derivative_from_output(double output) const
    {
        return sigmoid_beta_ * output * (1.0 - output);
    }

    std::vector<std::vector<double>> forward(std::span<const double> input) const
    {
        std::vector<std::vector<double>> activations;
        activations.reserve(layers_.size());
        activations.emplace_back(input.begin(), input.end());

        for (std::size_t layer = 0; layer < weights_.size(); ++layer)
        {
            std::vector<double> output(weights_[layer].rows, 0.0);
            for (std::size_t row = 0; row < weights_[layer].rows; ++row)
            {
                double sum = biases_[layer][row];
                for (std::size_t col = 0; col < weights_[layer].cols; ++col)
                {
                    sum += weights_[layer](row, col) * activations.back()[col];
                }
                output[row] = sigmoid(sum);
            }
            activations.push_back(std::move(output));
        }
        return activations;
    }

    void forward_training(std::span<const double> input)
    {
        std::copy(input.begin(), input.end(), activations_.front().begin());
        for (std::size_t layer = 0; layer < weights_.size(); ++layer)
        {
            for (std::size_t row = 0; row < weights_[layer].rows; ++row)
            {
                double sum = biases_[layer][row];
                for (std::size_t col = 0; col < weights_[layer].cols; ++col)
                {
                    sum += weights_[layer](row, col) * activations_[layer][col];
                }
                activations_[layer + 1][row] = sigmoid(sum);
            }
        }
    }

    const gradients& compute_gradients(std::span<const double> input, std::span<const double> expected)
    {
        validate_sample(input, expected);
        forward_training(input);

        for (std::size_t index = 0; index < layers_.back(); ++index)
        {
            const double output = activations_.back()[index];
            deltas_.back()[index] = (output - expected[index]) * sigmoid_derivative_from_output(output);
        }

        for (std::size_t layer = weights_.size() - 1; layer > 0; --layer)
        {
            const std::size_t current = layer - 1;
            for (std::size_t index = 0; index < layers_[current + 1]; ++index)
            {
                double propagated = 0.0;
                for (std::size_t next = 0; next < layers_[layer + 1]; ++next)
                {
                    propagated += weights_[layer](next, index) * deltas_[layer][next];
                }
                deltas_[current][index] = propagated * sigmoid_derivative_from_output(activations_[current + 1][index]);
            }
        }

        for (std::size_t layer = 0; layer < weights_.size(); ++layer)
        {
            for (std::size_t row = 0; row < weights_[layer].rows; ++row)
            {
                for (std::size_t col = 0; col < weights_[layer].cols; ++col)
                {
                    gradient_buffer_.weights[layer](row, col) = deltas_[layer][row] * activations_[layer][col];
                }
                gradient_buffer_.biases[layer][row] = deltas_[layer][row];
            }
        }
        return gradient_buffer_;
    }

    void apply_gradients(const gradients& gradient)
    {
        for (std::size_t layer = 0; layer < weights_.size(); ++layer)
        {
            for (std::size_t index = 0; index < weights_[layer].data.size(); ++index)
            {
                const double update = -learning_rate_ * gradient.weights[layer].data[index] +
                                      momentum_ * velocity_weights_[layer].data[index];
                weights_[layer].data[index] += update;
                velocity_weights_[layer].data[index] = update;
            }
            for (std::size_t index = 0; index < biases_[layer].size(); ++index)
            {
                const double update =
                    -learning_rate_ * gradient.biases[layer][index] + momentum_ * velocity_biases_[layer][index];
                biases_[layer][index] += update;
                velocity_biases_[layer][index] = update;
            }
        }
    }

    double dataset_rmse(const std::vector<sample>& dataset) const
    {
        if (dataset.empty())
        {
            return 0.0;
        }
        double squared_error = 0.0;
        std::size_t values = 0;
        for (const auto& entry : dataset)
        {
            const auto output = predict(entry.input);
            for (std::size_t index = 0; index < output.size(); ++index)
            {
                const double difference = entry.expected[index] - output[index];
                squared_error += difference * difference;
                ++values;
            }
        }
        return std::sqrt(squared_error / static_cast<double>(values));
    }

    double sample_loss(std::span<const double> input, std::span<const double> expected) const
    {
        const auto output = predict(input);
        double loss = 0.0;
        for (std::size_t index = 0; index < output.size(); ++index)
        {
            const double difference = output[index] - expected[index];
            loss += 0.5 * difference * difference;
        }
        return loss;
    }

    void validate_input(std::span<const double> input) const
    {
        if (input.size() != layers_.front())
        {
            throw std::invalid_argument("input size does not match the input layer");
        }
        validate_finite(input, "input");
    }

    void validate_sample(std::span<const double> input, std::span<const double> expected) const
    {
        validate_input(input);
        if (expected.size() != layers_.back())
        {
            throw std::invalid_argument("expected output size does not match the output layer");
        }
        validate_finite(expected, "expected output");
    }

    static void validate_finite(std::span<const double> values, const char* label)
    {
        if (std::any_of(values.begin(), values.end(), [](double value) { return !std::isfinite(value); }))
        {
            throw std::invalid_argument(std::string(label) + " values must be finite");
        }
    }

    static void validate_target_error(double target_error)
    {
        if (!std::isfinite(target_error) || target_error < 0.0)
        {
            throw std::invalid_argument("target error must be finite and non-negative");
        }
    }

    static double relative_error(double analytical, double numerical)
    {
        const double scale = std::max({1e-12, std::abs(analytical), std::abs(numerical)});
        return std::abs(analytical - numerical) / scale;
    }
};
} // namespace vstd
