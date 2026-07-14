#include "datapipe/pipeline.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void print_help(std::ostream& output) {
    output << R"(datapipe - chunked, typed CSV processing

Usage:
  datapipe INPUT.csv [options] --output OUTPUT.csv

Operations:
  --filter EXPR           Filter, for example "temperature >= 20"
  --select COLUMNS        Comma-separated output columns
  --group-by COLUMN       Group aggregation results by one column
  --aggregate SPECS       Comma-separated specs such as mean:value,count:*

Input and execution:
  --schema FIELDS         Explicit name:type[?] fields (int, double, string, bool)
  --delimiter CHAR        CSV delimiter (default: comma)
  --null-value TEXT       Additional input null marker and output null spelling
  --chunk-size N          Rows per processing chunk (default: 50000)
  --threads N             Worker threads (default: 1)
  --output PATH           Output CSV path (must differ from input)
  -h, --help              Show this help

Examples:
  datapipe readings.csv --filter "temperature > 20" \
    --select region,temperature --output warm.csv
  datapipe readings.csv --group-by region \
    --aggregate mean:temperature,count:* --threads 4 --output summary.csv

Exit codes: 0 success, 2 command-line error, 3 data/filesystem error, 4 unexpected error.
)";
}

std::size_t parse_size(std::string_view text, std::string_view option) {
    if (text.empty())
        throw std::invalid_argument(std::string(option) + " requires a positive integer");
    try {
        std::size_t consumed = 0;
        const auto value = std::stoull(std::string(text), &consumed);
        if (consumed != text.size() || value == 0) {
            throw std::invalid_argument(std::string(option) + " requires a positive integer");
        }
        return static_cast<std::size_t>(value);
    } catch (const std::out_of_range&) {
        throw std::invalid_argument(std::string(option) + " requires a positive integer");
    }
}

std::vector<std::string> split_columns(std::string_view text) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find(',', start);
        auto value = std::string(
            text.substr(start, end == std::string_view::npos ? text.size() - start : end - start));
        const auto first = value.find_first_not_of(" \t");
        const auto last = value.find_last_not_of(" \t");
        if (first == std::string::npos)
            throw std::invalid_argument("--select contains an empty column");
        result.push_back(value.substr(first, last - first + 1));
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
    return result;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 1) {
        print_help(std::cerr);
        return 2;
    }
    try {
        datapipe::PipelineConfig config;
        std::filesystem::path input;
        std::filesystem::path output;

        for (int index = 1; index < argc; ++index) {
            const std::string_view argument(argv[index]);
            if (argument == "--help" || argument == "-h") {
                print_help(std::cout);
                return 0;
            }
            auto next = [&](std::string_view option) -> std::string_view {
                if (++index >= argc)
                    throw std::invalid_argument(std::string(option) + " requires a value");
                return argv[index];
            };
            if (argument == "--filter")
                config.filter = datapipe::parse_filter(next(argument));
            else if (argument == "--select")
                config.select = split_columns(next(argument));
            else if (argument == "--group-by")
                config.group_by = std::string(next(argument));
            else if (argument == "--aggregate")
                config.aggregations = datapipe::parse_aggregations(next(argument));
            else if (argument == "--schema")
                config.schema = datapipe::parse_schema(next(argument));
            else if (argument == "--chunk-size")
                config.chunk_size = parse_size(next(argument), argument);
            else if (argument == "--threads")
                config.threads = parse_size(next(argument), argument);
            else if (argument == "--delimiter") {
                const auto value = next(argument);
                if (value.size() != 1)
                    throw std::invalid_argument("--delimiter requires one character");
                config.csv.delimiter = value.front();
            } else if (argument == "--null-value")
                config.csv.null_value = std::string(next(argument));
            else if (argument == "--output")
                output = next(argument);
            else if (!argument.empty() && argument.front() == '-') {
                throw std::invalid_argument("unknown option '" + std::string(argument) + "'");
            } else if (input.empty())
                input = argument;
            else
                throw std::invalid_argument("only one input path may be provided");
        }
        if (input.empty())
            throw std::invalid_argument("an input CSV path is required");
        if (output.empty())
            throw std::invalid_argument("--output is required");
        if (config.group_by && config.aggregations.empty()) {
            throw std::invalid_argument("--group-by requires --aggregate");
        }

        const auto result = datapipe::process_csv(input, output, std::move(config));
        std::cout << "Processed " << result.input_rows << " rows in " << result.chunks
                  << " chunk(s); wrote " << result.output_rows << " rows to "
                  << result.output_path.string() << '\n';
        return 0;
    } catch (const std::invalid_argument& error) {
        std::cerr << "datapipe: " << error.what() << "\nTry 'datapipe --help' for usage.\n";
        return 2;
    } catch (const datapipe::DataError& error) {
        std::cerr << "datapipe: " << error.what() << '\n';
        return 3;
    } catch (const std::exception& error) {
        std::cerr << "datapipe: unexpected error: " << error.what() << '\n';
        return 4;
    }
}
