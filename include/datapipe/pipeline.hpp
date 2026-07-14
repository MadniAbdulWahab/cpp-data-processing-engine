#pragma once

#include "datapipe/csv.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace datapipe {

enum class CompareOp { equal, not_equal, less, less_equal, greater, greater_equal };
enum class AggregateKind { count, sum, minimum, maximum, mean };

struct Filter {
    std::string column;
    CompareOp operation{CompareOp::equal};
    std::string literal;
};

struct Aggregation {
    AggregateKind kind{AggregateKind::count};
    std::string column{"*"};
};

struct PipelineConfig {
    std::optional<Schema> schema;
    std::optional<Filter> filter;
    std::vector<std::string> select;
    std::optional<std::string> group_by;
    std::vector<Aggregation> aggregations;
    std::size_t chunk_size{50'000};
    std::size_t threads{1};
    CsvOptions csv;
};

struct ProcessingResult {
    std::size_t input_rows{0};
    std::size_t output_rows{0};
    std::size_t chunks{0};
    Schema output_schema;
    std::filesystem::path output_path;
};

[[nodiscard]] Filter parse_filter(std::string_view expression);
[[nodiscard]] std::vector<Aggregation> parse_aggregations(std::string_view expression);
[[nodiscard]] Schema parse_schema(std::string_view expression);

class Pipeline {
  public:
    explicit Pipeline(PipelineConfig config);
    [[nodiscard]] ProcessingResult run(const std::filesystem::path& input,
                                       const std::filesystem::path& output) const;

  private:
    PipelineConfig config_;
};

[[nodiscard]] ProcessingResult process_csv(const std::filesystem::path& input,
                                           const std::filesystem::path& output,
                                           PipelineConfig config = {});

} // namespace datapipe
