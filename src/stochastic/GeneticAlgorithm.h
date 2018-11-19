/**
 * GeneticAlgorithm.h
 *
 * By Sebastian Raaphorst, 2018.
 */

#pragma once

#include <omp.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

#include <boost/format.hpp>

#include "Candidate.h"
#include "Populator.h"
#include "PopulationSelector.h"
#include "RNG.h"

namespace vorpal::stochastic {
    /**
     * The fundamentals of running the genetic algorithm on the problem.
     * @tparam Fitness the measure of fitness for a candidate
     */
    template<typename T, typename Fitness=double>
    class GeneticAlgorithm final {
    public:
        using pointer_type = std::unique_ptr<T>;

        GeneticAlgorithm() = delete;

        struct Options {
            // The populator that handles actions such as mutations, crossovers, and generating random candidates.
            // MUST be set for the algorithm to function.
            std::unique_ptr<Populator<T>> populator = nullptr;

            // The  population size of each generation.
            size_t population_size = 2000;

            // The maximum number of generations to run.
            // Default: as many as possible.
            uint64_t max_generations = UINT64_MAX;

            // The probability that two candidates will breed.
            double crossover_probability = 0.3;

            // The selector that chooses who will be involved in cross over / breeding when it occurs.
            // The default is 2-tournament selection.
            std::unique_ptr<Selector<T>> selector = std::make_unique<KTournamentSelector<T>>(2);

            // The probability that a candidate produces from breeding will mutate.
            double mutation_probability = 0.1;

            // If a candidate achieves this fitness level or higher, the algorithm terminates successfully and the
            // candidate is returned.
            Fitness fitness_success_threshold;

            // If we have a candidate with fitness less than this number, kill it.
            // Default: never kill.
            Fitness fitness_death_threshold = 0;

            // If we have a candidate with fitness less than the floor of this number times the best solution,
            // then we kill it.
            // Default: never kill.
            double fitness_death_factor = 0;

            // After this many rounds without improvement, kill and start again.
            // Default: never kill.
            uint64_t permissible_dead_rounds = UINT64_MAX;

            // Output the best candidate's fitness and the number of rounds every this many roounds.
            uint64_t output_rounds = 1'000;
        };

        template<typename Opts>
        static pointer_type run(Opts&& options) {
            // Verify correct input.
            if (options.populator == nullptr)
                throw std::invalid_argument("must set a Populator");
            if (options.population_size % 2 == 1)
                throw std::invalid_argument("pppulation_size must be even");

            // Begin timing.
            const auto start = std::chrono::system_clock::now();

            auto &gen = RNG::getGenerator();
            std::uniform_real_distribution<double> probabilityGenerator;

            // Keep track of the number of rounds we haven't improved.
            uint64_t deadRounds = 0;

            // We want to store the best element seen to far, so keep the max generated by the population initializer.
            size_t max_init = 0;
            std::vector<pointer_type> prevGeneration{options.population_size};
            for (size_t i = 0; i < options.population_size; ++i) {
                prevGeneration[i] = std::move(options.populator->generate());
                if (prevGeneration[i]->fitness() > prevGeneration[max_init]->fitness()) {
                    max_init = i;
                }
            }

            // Now create the pointer and store.
            pointer_type best = options.populator->survive(prevGeneration[max_init]);

            // *** Begin a new generation ***
            for (size_t generation = 0; generation < options.max_generations - 1; ++generation) {
                // Create the candidates for the next generation.
                std::vector<pointer_type> nextGeneration{options.population_size};

                /** Most of the work that can be easily parallelized is here, and propagates forward. **/
                const size_t maxiters = options.population_size/2;
                #pragma omp parallel for default(shared)
                for (size_t i = 0; i < options.population_size/2; ++i) {
                    // Crossover if probability dictates.
                    if (probabilityGenerator(gen) < options.crossover_probability) {
                        const size_t p0Idx = options.selector->select(prevGeneration);
                        const auto &p0 = prevGeneration[p0Idx];
                        const size_t p1Idx = options.selector->select(prevGeneration);
                        const auto &p1 = prevGeneration[p1Idx];

                        auto [c0, c1] = options.populator->crossover(p0, p1);
                        nextGeneration[2*i] = (probabilityGenerator(gen) < options.mutation_probability) ?
                                std::move(options.populator->mutate(c0)) : std::move(c0);
                        nextGeneration[2*i+1] = (probabilityGenerator(gen) < options.mutation_probability) ?
                                            std::move(options.populator->mutate(c1)) : std::move(c1);
                    } else {
                        nextGeneration[2*i] = std::move(options.populator->survive(prevGeneration[2*i]));
                        nextGeneration[2*i+1] = std::move(options.populator->survive(prevGeneration[2*i+1]));
                    }
                }

                // Now get the fittest solution and see if it is fit enough.
                bool fitness_increased = false;
                const auto &fittest = *std::max_element(std::cbegin(nextGeneration), std::cend(nextGeneration),
                        [](const auto &s1, const auto &s2) { return s1->fitness() < s2->fitness(); });
                if (fittest->fitness() > best->fitness()) {
                    best = std::move(options.populator->survive(fittest));
                    fitness_increased = true;
                }
                if (best->fitness() >= options.fitness_success_threshold) {
                    std::cerr << "Solved at generation " << generation << '\n';
                    return std::move(best);
                }
                if (fitness_increased)
                    deadRounds = 0;
                else
                    ++deadRounds;


                // Kill off candidates that are not deemed worthy.
                const auto kill_threshold = std::max(
                        options.fitness_death_threshold,
                        static_cast<Fitness>(options.fitness_death_factor * best->fitness()));

                // Demise: is it time to euthanize?
                #pragma omp parallel for
                for (size_t i = 0; i < options.population_size; ++i)
                    if (deadRounds >= options.permissible_dead_rounds || nextGeneration[i]->fitness() <= kill_threshold)
                        nextGeneration[i] = std::move(options.populator->generate());
                if (deadRounds >= options.permissible_dead_rounds) {
                    std::cerr << "Killed everything\n";
                    #pragma omp atomic write
                    deadRounds = 0;
                }

                // Output if requested.
                if (generation % options.output_rounds == 0)
                    std::cerr << "Generation: " << generation
                              << ", fittest: " << best->fitness()
                              << ", dead rounds: " << deadRounds
                              << ", time elapsed: " << ((std::chrono::system_clock::now() - start).count() / 1e6) << "s"
                              << '\n';

                // Copy the new generation to the old and give it a shuffle.
                // The shuffle is not necessary, but since we add parents in pairs, it will add some randomness.
                prevGeneration = std::move(nextGeneration);
                std::shuffle(std::begin(prevGeneration), std::end(prevGeneration), gen);
            }

            // Too many iterations: fail and return the best solution found thus far.
            return best;
        }
    };
}


