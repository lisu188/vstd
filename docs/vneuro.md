# vneuro

\`vneuro.h\` provides a small header-only feed-forward neural network for deterministic experiments and lightweight
embedded use. The implementation owns all of its storage with standard containers and uses contiguous row-major weight
matrices.

## Model

- Fully connected feed-forward layers.
- Sigmoid activation with configurable beta.
- Biases on every non-input layer.
- Xavier/Glorot uniform weight initialization.
- Stochastic gradient descent with classical momentum.
- Deterministic per-network \`std::mt19937_64\` state.
- RMSE reporting for training and test datasets.

The loss used for gradient calculation is one-half squared error. Output deltas are calculated as
\`(output - target) * sigmoid'(output)\`; hidden deltas are propagated through the next layer's weights.

## Modern API

\`\`\`cpp
vstd::neuro<> network({2, 4, 1}, 0.5, 0.1, 1.0, 42);

network.add_training_sample({0.0, 0.0}, {0.0});
network.add_training_sample({0.0, 1.0}, {1.0});
network.add_training_sample({1.0, 0.0}, {1.0});
network.add_training_sample({1.0, 1.0}, {0.0});

network.train(20'000);
auto output = network.predict(std::vector<double>{0.0, 1.0});
\`\`\`

The constructor arguments are layers, learning rate, momentum, sigmoid beta, and optional seed.

\`train_until(target_error, max_epochs)\` bounds convergence attempts. Input, output and configuration dimensions are
validated and non-finite sample values are rejected.

## Legacy compatibility

The original constructor remains available:

\`\`\`cpp
vstd::neuro<> network(std::vector<int>{2, 4, 1}, alfa, beta, eta);
\`\`\`

It maps \`alfa\` to momentum, \`beta\` to sigmoid beta and \`eta\` to learning rate. \`add_teacher\`, \`add_test\`,
\`teach\`, \`erms\` and \`test\` remain as compatibility wrappers. The legacy error-target \`teach\` overload is bounded
to avoid an infinite training loop.

## Validation

\`gradient_check(input, expected)\` compares analytical gradients with central finite differences and returns the maximum
relative error. It is intended for tests and small diagnostic networks rather than production training.

\`tests/vneuro_tests.cpp\` covers configuration and dimension validation, deterministic seeds/copies, legacy API
compatibility, empty datasets, numerical gradient checking, loss reduction and deterministic XOR convergence.

## Benchmarks

Configure with \`-DVSTD_BUILD_BENCHMARKS=ON\` and run \`vneuro_bench\`. The benchmark covers prediction and training for
2-4-1, 64-64-32, 128-256-128-32 and 784-128-64-10 networks.
