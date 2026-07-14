#include "datapipe/pipeline.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Case {
    std::string name;
    datapipe::PipelineConfig config;
};

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: datapipe_benchmark DATASET.csv [iterations]\n";
        return 2;
    }
    try {
        const std::filesystem::path input = argv[1];
        const std::size_t iterations = argc == 3 ? std::stoull(argv[2]) : 3;
        if (iterations == 0)
            throw std::invalid_argument("iterations must be positive");
        const std::size_t multiple_threads = std::max(1u, std::thread::hardware_concurrency());

        std::vector<Case> cases;
        for (const auto chunk_size : {std::size_t{1'000}, std::size_t{50'000}}) {
            for (const auto threads : {std::size_t{1}, multiple_threads}) {
                datapipe::PipelineConfig filter;
                filter.filter = datapipe::parse_filter("measurement >= 50");
                filter.chunk_size = chunk_size;
                filter.threads = threads;
                cases.push_back({"filter", filter});

                auto projection = filter;
                projection.select = {"id", "category", "measurement"};
                cases.push_back({"filter_projection", projection});

                datapipe::PipelineConfig aggregate;
                aggregate.group_by = "category";
                aggregate.aggregations = datapipe::parse_aggregations(
                    "count:*,sum:measurement,min:measurement,max:measurement,mean:measurement");
                aggregate.chunk_size = chunk_size;
                aggregate.threads = threads;
                cases.push_back({"group_aggregate", aggregate});
            }
        }

        std::cout << "case,chunk_size,threads,iteration,input_rows,output_rows,milliseconds\n";
        std::size_t output_sequence = 0;
        for (const auto& item : cases) {
            for (std::size_t iteration = 1; iteration <= iterations; ++iteration) {
                const auto output =
                    std::filesystem::temp_directory_path() /
                    ("datapipe_benchmark_" + std::to_string(output_sequence++) + ".csv");
                const auto start = std::chrono::steady_clock::now();
                const auto result = datapipe::process_csv(input, output, item.config);
                const auto finish = std::chrono::steady_clock::now();
                const auto milliseconds =
                    std::chrono::duration<double, std::milli>(finish - start).count();
                std::filesystem::remove(output);
                std::cout << item.name << ',' << item.config.chunk_size << ','
                          << item.config.threads << ',' << iteration << ',' << result.input_rows
                          << ',' << result.output_rows << ',' << milliseconds << '\n';
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "benchmark: " << error.what() << '\n';
        return 1;
    }
}
