#include "datapipe/types.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <type_traits>
#include <unordered_set>

namespace datapipe {
namespace {

std::string lower(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

[[noreturn]] void conversion_error(std::size_t row, std::string_view column, DataType type,
                                   std::string_view value) {
    std::ostringstream message;
    message << "row " << row << ", column '" << column << "': expected " << to_string(type)
            << ", got '" << value << "'";
    throw DataError(message.str());
}

} // namespace

std::string to_string(DataType type) {
    switch (type) {
    case DataType::integer:
        return "integer";
    case DataType::floating:
        return "floating";
    case DataType::string:
        return "string";
    case DataType::boolean:
        return "boolean";
    }
    throw std::logic_error("unknown data type");
}

DataType data_type_from_string(std::string_view text) {
    const auto value = lower(text);
    if (value == "int" || value == "integer" || value == "int64")
        return DataType::integer;
    if (value == "float" || value == "double" || value == "floating")
        return DataType::floating;
    if (value == "string" || value == "str")
        return DataType::string;
    if (value == "bool" || value == "boolean")
        return DataType::boolean;
    throw DataError("unknown data type '" + std::string(text) + "'");
}

bool is_null(const Value& value) noexcept { return std::holds_alternative<std::monostate>(value); }

std::string value_to_string(const Value& value) {
    return std::visit(
        [](const auto& item) -> std::string {
            using Type = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<Type, std::monostate>) {
                return {};
            } else if constexpr (std::is_same_v<Type, bool>) {
                return item ? "true" : "false";
            } else if constexpr (std::is_same_v<Type, double>) {
                if (!std::isfinite(item))
                    throw DataError("cannot serialize a non-finite number");
                std::ostringstream output;
                output << std::setprecision(std::numeric_limits<double>::max_digits10) << item;
                return output.str();
            } else if constexpr (std::is_same_v<Type, std::string>) {
                return item;
            } else {
                return std::to_string(item);
            }
        },
        value);
}

Value parse_value(std::string_view text, DataType type, std::size_t row, std::string_view column) {
    if (text.empty())
        return std::monostate{};
    if (type == DataType::string)
        return std::string(text);

    if (type == DataType::integer) {
        std::int64_t result{};
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
        if (error != std::errc{} || end != text.data() + text.size()) {
            conversion_error(row, column, type, text);
        }
        return result;
    }
    if (type == DataType::floating) {
        double result{};
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result,
                                                  std::chars_format::general);
        if (error != std::errc{} || end != text.data() + text.size() || !std::isfinite(result)) {
            conversion_error(row, column, type, text);
        }
        return result;
    }
    const auto normalized = lower(text);
    if (normalized == "true" || normalized == "1")
        return true;
    if (normalized == "false" || normalized == "0")
        return false;
    conversion_error(row, column, type, text);
}

DataType infer_type(std::string_view text) {
    if (text.empty())
        return DataType::string;
    const auto normalized = lower(text);
    if (normalized == "true" || normalized == "false")
        return DataType::boolean;
    std::int64_t integer{};
    const auto [integer_end, integer_error] =
        std::from_chars(text.data(), text.data() + text.size(), integer);
    if (integer_error == std::errc{} && integer_end == text.data() + text.size()) {
        return DataType::integer;
    }
    double floating{};
    const auto [floating_end, floating_error] = std::from_chars(
        text.data(), text.data() + text.size(), floating, std::chars_format::general);
    if (floating_error == std::errc{} && floating_end == text.data() + text.size() &&
        std::isfinite(floating)) {
        return DataType::floating;
    }
    return DataType::string;
}

Schema::Schema(std::vector<Field> fields) : fields_(std::move(fields)) {
    std::unordered_set<std::string> names;
    for (const auto& field : fields_) {
        if (field.name.empty())
            throw DataError("schema field names must not be empty");
        if (!names.emplace(field.name).second) {
            throw DataError("duplicate schema field '" + field.name + "'");
        }
    }
}

const Field& Schema::at(std::size_t index) const {
    if (index >= fields_.size())
        throw DataError("schema field index is out of range");
    return fields_[index];
}

std::size_t Schema::index_of(std::string_view name) const {
    const auto found = std::find_if(fields_.begin(), fields_.end(),
                                    [name](const Field& field) { return field.name == name; });
    if (found == fields_.end())
        throw DataError("unknown column '" + std::string(name) + "'");
    return static_cast<std::size_t>(std::distance(fields_.begin(), found));
}

} // namespace datapipe
