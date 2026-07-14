#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace datapipe {

enum class DataType { integer, floating, string, boolean };

[[nodiscard]] std::string to_string(DataType type);
[[nodiscard]] DataType data_type_from_string(std::string_view text);

using Value = std::variant<std::monostate, std::int64_t, double, std::string, bool>;

[[nodiscard]] bool is_null(const Value& value) noexcept;
[[nodiscard]] std::string value_to_string(const Value& value);
[[nodiscard]] Value parse_value(std::string_view text, DataType type, std::size_t row,
                                std::string_view column);
[[nodiscard]] DataType infer_type(std::string_view text);

struct Field {
    std::string name;
    DataType type{DataType::string};
    bool nullable{true};
};

class Schema {
  public:
    Schema() = default;
    explicit Schema(std::vector<Field> fields);

    [[nodiscard]] const std::vector<Field>& fields() const noexcept { return fields_; }
    [[nodiscard]] std::size_t size() const noexcept { return fields_.size(); }
    [[nodiscard]] bool empty() const noexcept { return fields_.empty(); }
    [[nodiscard]] const Field& at(std::size_t index) const;
    [[nodiscard]] std::size_t index_of(std::string_view name) const;

  private:
    std::vector<Field> fields_;
};

using Row = std::vector<Value>;

struct RecordBatch {
    std::vector<Row> rows;
    std::size_t first_source_row{0};
};

class DataError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

} // namespace datapipe
