#pragma once

#include "datapipe/types.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace datapipe {

struct CsvOptions {
    char delimiter{','};
    std::string null_value{};
};

class CsvReader {
  public:
    CsvReader(const std::filesystem::path& path, Schema schema, CsvOptions options = {});

    CsvReader(const CsvReader&) = delete;
    CsvReader& operator=(const CsvReader&) = delete;
    CsvReader(CsvReader&&) noexcept = default;
    CsvReader& operator=(CsvReader&&) noexcept = default;

    [[nodiscard]] const Schema& schema() const noexcept { return schema_; }
    [[nodiscard]] const std::vector<std::string>& header() const noexcept { return header_; }
    [[nodiscard]] std::optional<RecordBatch> read_batch(std::size_t max_rows);

    static Schema infer_schema(const std::filesystem::path& path, CsvOptions options = {},
                               std::size_t sample_rows = 1000);

  private:
    [[nodiscard]] std::optional<std::vector<std::string>> read_record();
    void read_and_validate_header();

    std::ifstream input_;
    Schema schema_;
    CsvOptions options_;
    std::vector<std::string> header_;
    std::size_t physical_line_{1};
    std::size_t data_row_{0};
};

class CsvWriter {
  public:
    CsvWriter(const std::filesystem::path& path, const Schema& schema, CsvOptions options = {});

    CsvWriter(const CsvWriter&) = delete;
    CsvWriter& operator=(const CsvWriter&) = delete;
    CsvWriter(CsvWriter&&) noexcept = default;
    CsvWriter& operator=(CsvWriter&&) noexcept = default;

    void write_row(const Row& row);
    void flush();

  private:
    void write_fields(const std::vector<std::string>& fields);

    std::ofstream output_;
    Schema schema_;
    CsvOptions options_;
};

} // namespace datapipe
