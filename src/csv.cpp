#include "datapipe/csv.hpp"

#include <algorithm>
#include <sstream>
#include <unordered_set>

namespace datapipe {
namespace {

std::ifstream open_input(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw DataError("unable to open input file '" + path.string() + "'");
    return stream;
}

std::ofstream open_output(const std::filesystem::path& path) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream)
        throw DataError("unable to open output file '" + path.string() + "'");
    return stream;
}

std::string quote(std::string_view value, char delimiter) {
    const bool needs_quotes = value.find(delimiter) != std::string_view::npos ||
                              value.find('"') != std::string_view::npos ||
                              value.find('\n') != std::string_view::npos ||
                              value.find('\r') != std::string_view::npos;
    if (!needs_quotes)
        return std::string(value);
    std::string result;
    result.reserve(value.size() + 2);
    result.push_back('"');
    for (const char character : value) {
        if (character == '"')
            result.push_back('"');
        result.push_back(character);
    }
    result.push_back('"');
    return result;
}

} // namespace

CsvReader::CsvReader(const std::filesystem::path& path, Schema schema, CsvOptions options)
    : input_(open_input(path)), schema_(std::move(schema)), options_(std::move(options)) {
    if (options_.delimiter == '"' || options_.delimiter == '\n' || options_.delimiter == '\r') {
        throw DataError("invalid CSV delimiter");
    }
    read_and_validate_header();
}

std::optional<std::vector<std::string>> CsvReader::read_record() {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    bool after_quote = false;
    bool read_any = false;

    while (true) {
        const int next = input_.get();
        if (next == EOF) {
            if (quoted) {
                throw DataError("malformed CSV at physical line " + std::to_string(physical_line_) +
                                ": unterminated quoted field");
            }
            if (!read_any && field.empty() && fields.empty())
                return std::nullopt;
            fields.push_back(std::move(field));
            return fields;
        }
        read_any = true;
        const char character = static_cast<char>(next);
        if (quoted) {
            if (character == '"') {
                if (input_.peek() == '"') {
                    input_.get();
                    field.push_back('"');
                } else {
                    quoted = false;
                    after_quote = true;
                }
            } else {
                if (character == '\n')
                    ++physical_line_;
                field.push_back(character);
            }
            continue;
        }
        if (after_quote) {
            if (character == options_.delimiter) {
                fields.push_back(std::move(field));
                field.clear();
                after_quote = false;
                continue;
            }
            if (character == '\n' || character == '\r') {
                if (character == '\r' && input_.peek() == '\n')
                    input_.get();
                ++physical_line_;
                fields.push_back(std::move(field));
                return fields;
            }
            throw DataError("malformed CSV at physical line " + std::to_string(physical_line_) +
                            ": unexpected character after closing quote");
        }
        if (character == '"') {
            if (!field.empty()) {
                throw DataError("malformed CSV at physical line " + std::to_string(physical_line_) +
                                ": quote inside an unquoted field");
            }
            quoted = true;
        } else if (character == options_.delimiter) {
            fields.push_back(std::move(field));
            field.clear();
        } else if (character == '\n' || character == '\r') {
            if (character == '\r' && input_.peek() == '\n')
                input_.get();
            ++physical_line_;
            fields.push_back(std::move(field));
            return fields;
        } else {
            field.push_back(character);
        }
    }
}

void CsvReader::read_and_validate_header() {
    auto record = read_record();
    if (!record)
        throw DataError("CSV input is empty and has no header");
    header_ = std::move(*record);
    if (!header_.empty() && header_[0].size() >= 3 &&
        static_cast<unsigned char>(header_[0][0]) == 0xEF &&
        static_cast<unsigned char>(header_[0][1]) == 0xBB &&
        static_cast<unsigned char>(header_[0][2]) == 0xBF) {
        header_[0].erase(0, 3);
    }
    std::unordered_set<std::string> names;
    for (const auto& name : header_) {
        if (name.empty())
            throw DataError("CSV header contains an empty column name");
        if (!names.emplace(name).second)
            throw DataError("duplicate CSV column '" + name + "'");
    }
    if (schema_.size() != header_.size()) {
        throw DataError("CSV header has " + std::to_string(header_.size()) +
                        " columns but schema has " + std::to_string(schema_.size()));
    }
    for (std::size_t i = 0; i < header_.size(); ++i) {
        if (header_[i] != schema_.at(i).name) {
            throw DataError("CSV column " + std::to_string(i + 1) + " is '" + header_[i] +
                            "' but schema expects '" + schema_.at(i).name + "'");
        }
    }
}

std::optional<RecordBatch> CsvReader::read_batch(std::size_t max_rows) {
    if (max_rows == 0)
        throw DataError("batch size must be greater than zero");
    RecordBatch batch;
    batch.first_source_row = data_row_ + 1;
    batch.rows.reserve(max_rows);
    while (batch.rows.size() < max_rows) {
        auto record = read_record();
        if (!record)
            break;
        ++data_row_;
        if (record->size() != schema_.size()) {
            throw DataError("row " + std::to_string(data_row_) + " has " +
                            std::to_string(record->size()) + " fields; expected " +
                            std::to_string(schema_.size()));
        }
        Row row;
        row.reserve(record->size());
        for (std::size_t i = 0; i < record->size(); ++i) {
            const auto& field = schema_.at(i);
            const auto& raw = record->at(i);
            if (raw.empty() || (!options_.null_value.empty() && raw == options_.null_value)) {
                if (!field.nullable) {
                    throw DataError("row " + std::to_string(data_row_) + ", column '" + field.name +
                                    "': null is not allowed");
                }
                row.emplace_back(std::monostate{});
            } else {
                row.push_back(parse_value(raw, field.type, data_row_, field.name));
            }
        }
        batch.rows.push_back(std::move(row));
    }
    if (batch.rows.empty())
        return std::nullopt;
    return batch;
}

Schema CsvReader::infer_schema(const std::filesystem::path& path, CsvOptions options,
                               std::size_t sample_rows) {
    if (sample_rows == 0)
        throw DataError("schema inference sample size must be greater than zero");
    auto input = open_input(path);
    input.close();

    // Use the normal parser for sampling so inference follows the same CSV rules.
    std::ifstream header_stream = open_input(path);
    std::string first_line;
    if (!std::getline(header_stream, first_line))
        throw DataError("CSV input is empty and has no header");
    header_stream.close();

    // Count header fields before constructing the temporary all-string schema.
    std::vector<std::string> names;
    {
        std::ifstream stream = open_input(path);
        std::string field;
        bool quoted = false;
        while (true) {
            const int value = stream.get();
            if (value == EOF)
                throw DataError("CSV input is empty and has no header");
            const char character = static_cast<char>(value);
            if (quoted) {
                if (character == '"') {
                    if (stream.peek() == '"') {
                        stream.get();
                        field.push_back('"');
                    } else
                        quoted = false;
                } else
                    field.push_back(character);
            } else if (character == '"' && field.empty())
                quoted = true;
            else if (character == options.delimiter) {
                names.push_back(std::move(field));
                field.clear();
            } else if (character == '\n' || character == '\r') {
                names.push_back(std::move(field));
                break;
            } else
                field.push_back(character);
        }
    }
    std::vector<Field> string_fields;
    string_fields.reserve(names.size());
    for (const auto& name : names)
        string_fields.push_back({name, DataType::string, true});
    CsvReader reader(path, Schema(std::move(string_fields)), options);

    std::vector<std::optional<DataType>> inferred(names.size());
    std::vector<bool> nullable(names.size(), false);
    std::size_t seen = 0;
    while (seen < sample_rows) {
        auto batch = reader.read_batch(std::min<std::size_t>(sample_rows - seen, 256));
        if (!batch)
            break;
        for (const auto& row : batch->rows) {
            ++seen;
            for (std::size_t i = 0; i < row.size(); ++i) {
                if (is_null(row[i])) {
                    nullable[i] = true;
                    continue;
                }
                const auto type = infer_type(std::get<std::string>(row[i]));
                if (!inferred[i])
                    inferred[i] = type;
                else if (*inferred[i] != type) {
                    if ((*inferred[i] == DataType::integer && type == DataType::floating) ||
                        (*inferred[i] == DataType::floating && type == DataType::integer)) {
                        inferred[i] = DataType::floating;
                    } else
                        inferred[i] = DataType::string;
                }
            }
        }
    }
    std::vector<Field> fields;
    fields.reserve(names.size());
    for (std::size_t i = 0; i < names.size(); ++i) {
        fields.push_back(
            {names[i], inferred[i].value_or(DataType::string), nullable[i] || !inferred[i]});
    }
    return Schema(std::move(fields));
}

CsvWriter::CsvWriter(const std::filesystem::path& path, const Schema& schema, CsvOptions options)
    : output_(open_output(path)), schema_(schema), options_(std::move(options)) {
    std::vector<std::string> header;
    header.reserve(schema_.size());
    for (const auto& field : schema_.fields())
        header.push_back(field.name);
    write_fields(header);
}

void CsvWriter::write_fields(const std::vector<std::string>& fields) {
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i != 0)
            output_.put(options_.delimiter);
        output_ << quote(fields[i], options_.delimiter);
    }
    output_.put('\n');
    if (!output_)
        throw DataError("failed while writing CSV output");
}

void CsvWriter::write_row(const Row& row) {
    if (row.size() != schema_.size())
        throw DataError("output row does not match output schema");
    std::vector<std::string> fields;
    fields.reserve(row.size());
    for (const auto& value : row)
        fields.push_back(is_null(value) ? options_.null_value : value_to_string(value));
    write_fields(fields);
}

void CsvWriter::flush() {
    output_.flush();
    if (!output_)
        throw DataError("failed to flush CSV output");
}

} // namespace datapipe
