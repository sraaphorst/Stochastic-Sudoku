/**
 * HillClimbinglgorithm.h
 *
 * By Sebastian Raaphorst, 2018.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "HillClimbingOptions.h"
#include "HillClimbingPopulator.h"
#include "RNG.h"

namespace vorpal::stochastic {
    /**
     * A simple generalization of hill-climbing.
     * This allows us to implement a number of different hill-climbing like algorithms in a single strategy.
     */
    template<typename T,
            typename Fitness = size_t,
            typename Options = HillClimbingOptions<T, Fitness>,
            typename State = void*>
    class HillClimbingAlgorithm {
        static_assert(std::is_arithmetic_v<Fitness>);
    public:
        using option_type = Options;
        using pointer_type = std::unique_ptr<T>;

    protected:
        std::unique_ptr<option_type> options;

    public:
        HillClimbingAlgorithm() = default;

        template<typename Opts>
        pointer_type run(Opts &&options) {
            // Verify correct input.
            if (options.populator == nullptr)
                throw std::invalid_argument("must set an HillClimbingPopulator");

            // Keep track of the best candidate seen so far.
            pointer_type best = nullptr;

            for (uint64_t round = 0; round < options.max_rounds; ++round) {
                // Initialize the state.
                auto state = initState(options);

                // Create the original candidate.
                pointer_type cur = std::move(options.populator->generate());

                for (uint64_t iteration = 0; iteration < options.max_iterations_per_round; ++iteration) {
                    // Get the next candidate and determine if we should move to it.
                    pointer_type next = std::move(options.populator->generateNeighbour(cur));
                    if (accept(next, cur, options, state))
                        cur = std::move(next);

                    // Check if we are considered a solution.
                    if (cur->fitness() >= options.fitness_success_threshold)
                        return cur;

                    // Check if we are the best seen so far, and if so, copy.
                    if (!best || best->fitness() < cur->fitness()) {
                        best = std::make_unique<T>(*cur);
                        std::cerr << "Best: " << best->fitness() << '\n';
                    }
                }
            }

            // If we reach this point, we fail. Return the best solution found thus far.
            return best;
        }

    protected:
        /**
         * Create an initial state to store data about the heuristic
         * @return the initialized state
         */
        virtual std::unique_ptr<State> initState(const option_type&) {
            return std::make_unique<State>();
        }

        /**
         * Determine if we should accept a next state.
         * Heuristics with more complicated rules should adjust this accordingly and adjust their state.
         * @param next candidate state to accept
         * @param cur current state state
         * @return true to accept the next state, false to reject it
         */
        virtual bool accept(const pointer_type &next, const pointer_type &cur, const option_type&, std::unique_ptr<State>&) {
            return next->fitness() > cur->fitness();
        }
    };
}