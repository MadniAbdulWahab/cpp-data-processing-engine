#include "datapipe/csv.hpp"
#include "datapipe/pipeline.hpp"
#include "datapipe/thread_pool.hpp"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct TestFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition))                                                                          \
            throw TestFailure(std::string("check failed: ") + #condition);                         \
    } while (false)

template <typename Exception, typename Function> void check_throws(Function&& function) {
    try {
        function();
    } catch (const Exception&) {
        return;
    }
    throw TestFailure("expected exception was not thrown");
}

std::string read_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

fs::path temp_path(std::string_view name) {
    static std::atomic<unsigned> sequence{0};
    return fs::temp_directory_path() /
           ("datapipe_test_" + std::to_string(sequence++) + "_" + std::string(name));
}

void write_file(const fs::path& path, std::string_view contents) {
    std::ofstream output(path, std::ios::binary);
    output << contents;
}

const fs::path fixtures = DATAPIPE_TEST_FIXTURES;

void test_schema_and_values() {
    datapipe::Schema schema({{"id", datapipe::DataType::integer, false},
                             {"value", datapipe::DataType::floating, true},
                             {"name", datapipe::DataType::string, true},
                             {"active", datapipe::DataType::boolean, false}});
    CHECK(schema.size() == 4);
    CHECK(schema.index_of("value") == 1);
    CHECK(std::get<std::int64_t>(
              datapipe::parse_value("42", datapipe::DataType::integer, 1, "id")) == 42);
    CHECK(std::get<double>(
              datapipe::parse_value("2.5", datapipe::DataType::floating, 1, "value")) == 2.5);
    CHECK(std::get<bool>(
              datapipe::parse_value("false", datapipe::DataType::boolean, 1, "active")) == false);
    CHECK(datapipe::is_null(datapipe::parse_value("", datapipe::DataType::string, 1, "name")));
    check_throws<datapipe::DataError>([] {
        datapipe::Schema invalid(
            {{"x", datapipe::DataType::string, true}, {"x", datapipe::DataType::string, true}});
    });
    check_throws<datapipe::DataError>(
        [] { (void)datapipe::parse_value("oops", datapipe::DataType::integer, 7, "id"); });
}

void test_csv_features_and_inference() {
    const auto schema = datapipe::CsvReader::infer_schema(fixtures / "basic.csv");
    CHECK(schema.at(0).type == datapipe::DataType::integer);
    CHECK(schema.at(2).type == datapipe::DataType::floating);
    CHECK(schema.at(3).type == datapipe::DataType::boolean);
    datapipe::CsvReader reader(fixtures / "basic.csv", schema);
    auto batch = reader.read_batch(2);
    CHECK(batch && batch->rows.size() == 2);
    CHECK(std::get<std::string>(batch->rows[0][4]) == "clear, calm");
    CHECK(std::get<std::string>(batch->rows[1][4]) == "said \"cold\"");
    auto remainder = reader.read_batch(10);
    CHECK(remainder && remainder->rows.size() == 3);
    CHECK(datapipe::is_null(remainder->rows[0][4]));
    CHECK(datapipe::is_null(remainder->rows[2][2]));
    check_throws<datapipe::DataError>(
        [] { (void)datapipe::CsvReader::infer_schema(fixtures / "malformed.csv"); });

    const auto input = temp_path("required.csv");
    write_file(input, "id,name\n1,\n");
    {
        datapipe::CsvReader required(
            input, datapipe::Schema({{"id", datapipe::DataType::integer, false},
                                     {"name", datapipe::DataType::string, false}}));
        check_throws<datapipe::DataError>([&] { (void)required.read_batch(1); });
    }
    fs::remove(input);
}

void test_multiline_and_custom_delimiter() {
    const auto input = temp_path("multi.csv");
    write_file(input, "id;text\n1;\"line one\nline two\"\n");
    datapipe::CsvOptions options;
    options.delimiter = ';';
    const auto schema = datapipe::CsvReader::infer_schema(input, options);
    {
        datapipe::CsvReader reader(input, schema, options);
        const auto batch = reader.read_batch(4);
        CHECK(batch && std::get<std::string>(batch->rows[0][1]) == "line one\nline two");
    }
    fs::remove(input);
}

datapipe::PipelineConfig base_config(std::size_t threads = 1) {
    datapipe::PipelineConfig config;
    config.chunk_size = 2;
    config.threads = threads;
    return config;
}

std::string run_pipeline(datapipe::PipelineConfig config, std::string_view suffix = "out.csv") {
    const auto output = temp_path(suffix);
    const auto result = datapipe::process_csv(fixtures / "basic.csv", output, std::move(config));
    CHECK(result.input_rows == 5);
    const auto text = read_file(output);
    fs::remove(output);
    return text;
}

void test_all_filters_and_projection() {
    const std::vector<std::pair<std::string, std::size_t>> cases = {{"id == 3", 1}, {"id != 3", 4},
                                                                    {"id < 3", 2},  {"id <= 3", 3},
                                                                    {"id > 3", 2},  {"id >= 3", 3}};
    for (const auto& [expression, rows] : cases) {
        auto config = base_config();
        config.filter = datapipe::parse_filter(expression);
        config.select = {"id", "region"};
        const auto text = run_pipeline(config);
        CHECK(static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n')) == rows + 1);
    }
}

void test_aggregations_and_grouping() {
    auto config = base_config();
    config.group_by = "region";
    config.aggregations = datapipe::parse_aggregations(
        "count:*,sum:temperature,min:temperature,max:temperature,mean:temperature");
    const auto text = run_pipeline(config);
    CHECK(text.find(
              "region,count,sum_temperature,min_temperature,max_temperature,mean_temperature") ==
          0);
    CHECK(text.find("north,2,45.5,21.5,24,22.75") != std::string::npos);
    CHECK(text.find("south,2,18,18,18,18") != std::string::npos);
    CHECK(text.find("east,1,20.5,20.5,20.5,20.5") != std::string::npos);
}

void test_chunks_empty_header_and_single_record() {
    const auto header_only = temp_path("header.csv");
    const auto output = temp_path("header_out.csv");
    write_file(header_only, "id,name\n");
    auto config = base_config();
    config.schema = datapipe::parse_schema("id:int?,name:string?");
    auto result = datapipe::process_csv(header_only, output, config);
    CHECK(result.input_rows == 0 && result.chunks == 0 && result.output_rows == 0);
    CHECK(read_file(output) == "id,name\n");
    fs::remove(header_only);
    fs::remove(output);

    const auto single = temp_path("single.csv");
    const auto single_out = temp_path("single_out.csv");
    write_file(single, "id,name\n1,one\n");
    result = datapipe::process_csv(single, single_out, config);
    CHECK(result.input_rows == 1 && result.chunks == 1 && result.output_rows == 1);
    fs::remove(single);
    fs::remove(single_out);

    const auto empty = temp_path("empty.csv");
    const auto empty_out = temp_path("empty_out.csv");
    write_file(empty, "");
    result = datapipe::process_csv(empty, empty_out, config);
    CHECK(result.input_rows == 0 && result.chunks == 0 && read_file(empty_out) == "id,name\n");
    fs::remove(empty_out);
    check_throws<datapipe::DataError>(
        [&] { (void)datapipe::process_csv(empty, empty_out, base_config()); });
    fs::remove(empty);
}

void test_thread_pool() {
    datapipe::ThreadPool pool(3);
    auto first = pool.submit([] { return 21 * 2; });
    auto error = pool.submit([]() -> int { throw std::runtime_error("worker failure"); });
    CHECK(first.get() == 42);
    check_throws<std::runtime_error>([&] { (void)error.get(); });
    pool.shutdown();
    check_throws<std::runtime_error>([&] { (void)pool.submit([] {}); });
}

void test_parallel_equivalence_and_path_protection() {
    auto one = base_config(1);
    one.group_by = "region";
    one.aggregations = datapipe::parse_aggregations("mean:temperature,count:*");
    auto many = one;
    many.threads = 4;
    CHECK(run_pipeline(one, "one.csv") == run_pipeline(many, "many.csv"));
    check_throws<datapipe::DataError>([&] {
        (void)datapipe::process_csv(fixtures / "basic.csv", fixtures / "basic.csv", base_config());
    });
}

void test_validation() {
    auto config = base_config();
    config.aggregations = datapipe::parse_aggregations("sum:region");
    check_throws<datapipe::DataError>([&] { (void)run_pipeline(config); });
    check_throws<datapipe::DataError>([] { (void)datapipe::parse_filter("id approximately 2"); });
    check_throws<datapipe::DataError>([] { (void)datapipe::parse_aggregations("median:id"); });
}

} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"schema and values", test_schema_and_values},
        {"CSV features and inference", test_csv_features_and_inference},
        {"multiline and delimiter", test_multiline_and_custom_delimiter},
        {"filters and projection", test_all_filters_and_projection},
        {"aggregations and grouping", test_aggregations_and_grouping},
        {"chunks and edge sizes", test_chunks_empty_header_and_single_record},
        {"thread pool", test_thread_pool},
        {"parallel equivalence and paths", test_parallel_equivalence_and_path_protection},
        {"validation", test_validation}};
    std::size_t failures = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        }
    }
    std::cout << (tests.size() - failures) << "/" << tests.size() << " tests passed\n";
    return failures == 0 ? 0 : 1;
}
