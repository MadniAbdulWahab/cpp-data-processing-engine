#include "datapipe/pipeline.hpp"

#include "datapipe/thread_pool.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <deque>
#include <future>
#include <limits>
#include <map>
#include <sstream>
#include <system_error>
#include <type_traits>

namespace datapipe {
namespace {

std::string trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
        return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(first, last - first + 1));
}

std::vector<std::string> split(std::string_view text, char delimiter) {
    std::vector<std::string> values;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find(delimiter, start);
        values.push_back(trim(
            text.substr(start, end == std::string_view::npos ? text.size() - start : end - start)));
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
    return values;
}

std::string lower(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

struct ValueLess {
    bool operator()(const Value& left, const Value& right) const {
        if (left.index() != right.index())
            return left.index() < right.index();
        return std::visit(
            [](const auto& lhs, const auto& rhs) -> bool {
                using L = std::decay_t<decltype(lhs)>;
                using R = std::decay_t<decltype(rhs)>;
                if constexpr (std::is_same_v<L, R>) {
                    if constexpr (std::is_same_v<L, std::monostate>)
                        return false;
                    else
                        return lhs < rhs;
                } else {
                    return false;
                }
            },
            left, right);
    }
};

double numeric_value(const Value& value) {
    if (const auto* integer = std::get_if<std::int64_t>(&value))
        return static_cast<double>(*integer);
    if (const auto* floating = std::get_if<double>(&value))
        return *floating;
    throw DataError("internal error: expected a numeric value");
}

bool compare_values(const Value& left, const Value& right, CompareOp operation) {
    if (is_null(left) || is_null(right))
        return false;
    int ordering = 0;
    if ((std::holds_alternative<std::int64_t>(left) || std::holds_alternative<double>(left)) &&
        (std::holds_alternative<std::int64_t>(right) || std::holds_alternative<double>(right))) {
        const auto lhs = numeric_value(left);
        const auto rhs = numeric_value(right);
        ordering = lhs < rhs ? -1 : (lhs > rhs ? 1 : 0);
    } else if (left.index() == right.index()) {
        ordering = std::visit(
            [](const auto& lhs, const auto& rhs) -> int {
                using L = std::decay_t<decltype(lhs)>;
                using R = std::decay_t<decltype(rhs)>;
                if constexpr (std::is_same_v<L, R> && !std::is_same_v<L, std::monostate>) {
                    return lhs < rhs ? -1 : (lhs > rhs ? 1 : 0);
                } else {
                    return 0;
                }
            },
            left, right);
    } else {
        throw DataError("filter comparison has incompatible types");
    }
    switch (operation) {
    case CompareOp::equal:
        return ordering == 0;
    case CompareOp::not_equal:
        return ordering != 0;
    case CompareOp::less:
        return ordering < 0;
    case CompareOp::less_equal:
        return ordering <= 0;
    case CompareOp::greater:
        return ordering > 0;
    case CompareOp::greater_equal:
        return ordering >= 0;
    }
    return false;
}

struct AggregateState {
    std::int64_t count{0};
    double sum{0.0};
    std::optional<double> minimum;
    std::optional<double> maximum;

    void add(const Value& value, AggregateKind kind) {
        if (kind == AggregateKind::count) {
            if (!is_null(value))
                ++count;
            return;
        }
        if (is_null(value))
            return;
        const double number = numeric_value(value);
        ++count;
        if (kind == AggregateKind::sum || kind == AggregateKind::mean)
            sum += number;
        if (kind == AggregateKind::minimum)
            minimum = minimum ? std::min(*minimum, number) : number;
        if (kind == AggregateKind::maximum)
            maximum = maximum ? std::max(*maximum, number) : number;
    }

    void merge(const AggregateState& other, AggregateKind kind) {
        count += other.count;
        if (kind == AggregateKind::sum || kind == AggregateKind::mean)
            sum += other.sum;
        if (other.minimum)
            minimum = minimum ? std::min(*minimum, *other.minimum) : other.minimum;
        if (other.maximum)
            maximum = maximum ? std::max(*maximum, *other.maximum) : other.maximum;
    }

    Value finish(AggregateKind kind) const {
        switch (kind) {
        case AggregateKind::count:
            return count;
        case AggregateKind::sum:
            return count == 0 ? Value(std::monostate{}) : Value(sum);
        case AggregateKind::minimum:
            return minimum ? Value(*minimum) : Value(std::monostate{});
        case AggregateKind::maximum:
            return maximum ? Value(*maximum) : Value(std::monostate{});
        case AggregateKind::mean:
            return count == 0 ? Value(std::monostate{}) : Value(sum / static_cast<double>(count));
        }
        return std::monostate{};
    }
};

struct Prepared {
    Schema input_schema;
    Schema output_schema;
    std::optional<std::size_t> filter_index;
    std::optional<Value> filter_value;
    std::vector<std::size_t> selection;
    std::optional<std::size_t> group_index;
    std::vector<std::optional<std::size_t>> aggregate_indices;
};

std::string aggregate_name(const Aggregation& aggregation) {
    std::string prefix;
    switch (aggregation.kind) {
    case AggregateKind::count:
        prefix = "count";
        break;
    case AggregateKind::sum:
        prefix = "sum";
        break;
    case AggregateKind::minimum:
        prefix = "min";
        break;
    case AggregateKind::maximum:
        prefix = "max";
        break;
    case AggregateKind::mean:
        prefix = "mean";
        break;
    }
    return aggregation.column == "*" ? prefix : prefix + "_" + aggregation.column;
}

Prepared prepare(const Schema& schema, const PipelineConfig& config) {
    Prepared result;
    result.input_schema = schema;
    if (config.filter) {
        result.filter_index = schema.index_of(config.filter->column);
        const auto& field = schema.at(*result.filter_index);
        if (field.type == DataType::boolean && config.filter->operation != CompareOp::equal &&
            config.filter->operation != CompareOp::not_equal) {
            throw DataError("boolean filters support only == and !=");
        }
        result.filter_value = parse_value(config.filter->literal, field.type, 0, field.name);
        if (is_null(*result.filter_value))
            throw DataError("filter literal must not be empty");
    }
    for (const auto& name : config.select)
        result.selection.push_back(schema.index_of(name));
    if (result.selection.empty()) {
        for (std::size_t i = 0; i < schema.size(); ++i)
            result.selection.push_back(i);
    }
    if (config.group_by)
        result.group_index = schema.index_of(*config.group_by);

    std::vector<Field> output_fields;
    if (config.aggregations.empty()) {
        for (const auto index : result.selection)
            output_fields.push_back(schema.at(index));
    } else {
        if (result.group_index)
            output_fields.push_back(schema.at(*result.group_index));
        for (const auto& aggregation : config.aggregations) {
            if (aggregation.column == "*") {
                if (aggregation.kind != AggregateKind::count) {
                    throw DataError("only count may aggregate '*'");
                }
                result.aggregate_indices.emplace_back(std::nullopt);
            } else {
                const auto index = schema.index_of(aggregation.column);
                if (aggregation.kind != AggregateKind::count &&
                    schema.at(index).type != DataType::integer &&
                    schema.at(index).type != DataType::floating) {
                    throw DataError("aggregation '" + aggregate_name(aggregation) +
                                    "' requires a numeric column");
                }
                result.aggregate_indices.emplace_back(index);
            }
            output_fields.push_back(
                {aggregate_name(aggregation),
                 aggregation.kind == AggregateKind::count ? DataType::integer : DataType::floating,
                 aggregation.kind != AggregateKind::count});
        }
    }
    result.output_schema = Schema(std::move(output_fields));
    return result;
}

using Groups = std::map<Value, std::vector<AggregateState>, ValueLess>;

struct ChunkResult {
    std::size_t input_rows{0};
    std::vector<Row> rows;
    Groups groups;
};

ChunkResult process_batch(RecordBatch batch, const Prepared& prepared,
                          const PipelineConfig& config) {
    ChunkResult result;
    result.input_rows = batch.rows.size();
    for (const auto& row : batch.rows) {
        if (prepared.filter_index &&
            !compare_values(row[*prepared.filter_index], *prepared.filter_value,
                            config.filter->operation)) {
            continue;
        }
        if (config.aggregations.empty()) {
            Row projected;
            projected.reserve(prepared.selection.size());
            for (const auto index : prepared.selection)
                projected.push_back(row[index]);
            result.rows.push_back(std::move(projected));
            continue;
        }
        const Value key =
            prepared.group_index ? row[*prepared.group_index] : Value(std::monostate{});
        auto [entry, inserted] = result.groups.try_emplace(key);
        if (inserted)
            entry->second.resize(config.aggregations.size());
        for (std::size_t i = 0; i < config.aggregations.size(); ++i) {
            if (!prepared.aggregate_indices[i]) {
                entry->second[i].add(Value(std::int64_t{1}), config.aggregations[i].kind);
            } else {
                entry->second[i].add(row[*prepared.aggregate_indices[i]],
                                     config.aggregations[i].kind);
            }
        }
    }
    return result;
}

bool same_path(const std::filesystem::path& left, const std::filesystem::path& right) {
    std::error_code error;
    if (std::filesystem::exists(left, error) && std::filesystem::exists(right, error)) {
        if (std::filesystem::equivalent(left, right, error) && !error)
            return true;
    }
    return std::filesystem::absolute(left).lexically_normal() ==
           std::filesystem::absolute(right).lexically_normal();
}

} // namespace

Filter parse_filter(std::string_view expression) {
    static constexpr std::pair<std::string_view, CompareOp> operators[] = {
        {"<=", CompareOp::less_equal}, {">=", CompareOp::greater_equal},
        {"!=", CompareOp::not_equal},  {"==", CompareOp::equal},
        {"<", CompareOp::less},        {">", CompareOp::greater}};
    for (const auto& [token, operation] : operators) {
        const auto position = expression.find(token);
        if (position != std::string_view::npos) {
            auto column = trim(expression.substr(0, position));
            auto literal = trim(expression.substr(position + token.size()));
            if (column.empty() || literal.empty())
                throw DataError("invalid filter expression");
            if (literal.size() >= 2 && ((literal.front() == '"' && literal.back() == '"') ||
                                        (literal.front() == '\'' && literal.back() == '\''))) {
                literal = literal.substr(1, literal.size() - 2);
            }
            return {std::move(column), operation, std::move(literal)};
        }
    }
    throw DataError("filter must contain one of ==, !=, <, <=, >, >=");
}

std::vector<Aggregation> parse_aggregations(std::string_view expression) {
    std::vector<Aggregation> result;
    for (const auto& item : split(expression, ',')) {
        const auto separator = item.find(':');
        if (separator == std::string::npos)
            throw DataError("aggregation must use operation:column");
        const auto operation = lower(trim(std::string_view(item).substr(0, separator)));
        const auto column = trim(std::string_view(item).substr(separator + 1));
        if (column.empty())
            throw DataError("aggregation column must not be empty");
        AggregateKind kind;
        if (operation == "count")
            kind = AggregateKind::count;
        else if (operation == "sum")
            kind = AggregateKind::sum;
        else if (operation == "min" || operation == "minimum")
            kind = AggregateKind::minimum;
        else if (operation == "max" || operation == "maximum")
            kind = AggregateKind::maximum;
        else if (operation == "mean" || operation == "avg")
            kind = AggregateKind::mean;
        else
            throw DataError("unknown aggregation '" + operation + "'");
        result.push_back({kind, column});
    }
    if (result.empty())
        throw DataError("at least one aggregation is required");
    return result;
}

Schema parse_schema(std::string_view expression) {
    std::vector<Field> fields;
    for (const auto& item : split(expression, ',')) {
        const auto separator = item.find(':');
        if (separator == std::string::npos)
            throw DataError("schema fields must use name:type[?]");
        const auto name = trim(std::string_view(item).substr(0, separator));
        auto type_name = trim(std::string_view(item).substr(separator + 1));
        bool nullable = false;
        if (!type_name.empty() && type_name.back() == '?') {
            nullable = true;
            type_name.pop_back();
        }
        fields.push_back({name, data_type_from_string(type_name), nullable});
    }
    return Schema(std::move(fields));
}

Pipeline::Pipeline(PipelineConfig config) : config_(std::move(config)) {
    if (config_.chunk_size == 0)
        throw DataError("chunk size must be greater than zero");
    if (config_.threads == 0)
        throw DataError("thread count must be greater than zero");
}

ProcessingResult Pipeline::run(const std::filesystem::path& input,
                               const std::filesystem::path& output) const {
    if (same_path(input, output))
        throw DataError("refusing to overwrite the input file");
    std::error_code size_error;
    const auto input_size = std::filesystem::file_size(input, size_error);
    if (size_error)
        throw DataError("unable to inspect input file '" + input.string() + "'");
    if (input_size == 0 && !config_.schema) {
        throw DataError("cannot infer a schema from an empty input; provide --schema");
    }
    const Schema schema =
        config_.schema ? *config_.schema : CsvReader::infer_schema(input, config_.csv);
    const Prepared prepared = prepare(schema, config_);

    const auto parent = output.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent)) {
        throw DataError("output directory does not exist: '" + parent.string() + "'");
    }
    CsvWriter writer(output, prepared.output_schema, config_.csv);
    ThreadPool pool(config_.threads);
    std::deque<std::future<ChunkResult>> pending;
    Groups merged_groups;
    ProcessingResult result;
    result.output_schema = prepared.output_schema;
    result.output_path = output;

    if (input_size == 0) {
        if (!config_.aggregations.empty() && !config_.group_by) {
            Row row;
            for (const auto& aggregation : config_.aggregations) {
                row.push_back(AggregateState{}.finish(aggregation.kind));
            }
            writer.write_row(row);
            result.output_rows = 1;
        }
        writer.flush();
        return result;
    }

    CsvReader reader(input, schema, config_.csv);

    auto consume = [&] {
        ChunkResult chunk = pending.front().get();
        pending.pop_front();
        result.input_rows += chunk.input_rows;
        ++result.chunks;
        if (config_.aggregations.empty()) {
            for (const auto& row : chunk.rows)
                writer.write_row(row);
            result.output_rows += chunk.rows.size();
        } else {
            for (auto& [key, states] : chunk.groups) {
                auto [target, inserted] = merged_groups.try_emplace(std::move(key));
                if (inserted)
                    target->second.resize(config_.aggregations.size());
                for (std::size_t i = 0; i < states.size(); ++i) {
                    target->second[i].merge(states[i], config_.aggregations[i].kind);
                }
            }
        }
    };

    while (auto batch = reader.read_batch(config_.chunk_size)) {
        pending.push_back(pool.submit([batch = std::move(*batch), &prepared, this]() mutable {
            return process_batch(std::move(batch), prepared, config_);
        }));
        if (pending.size() >= config_.threads * 2)
            consume();
    }
    while (!pending.empty())
        consume();

    if (!config_.aggregations.empty()) {
        if (!config_.group_by && merged_groups.empty()) {
            merged_groups[Value(std::monostate{})] =
                std::vector<AggregateState>(config_.aggregations.size());
        }
        for (const auto& [key, states] : merged_groups) {
            Row row;
            if (prepared.group_index)
                row.push_back(key);
            for (std::size_t i = 0; i < states.size(); ++i) {
                row.push_back(states[i].finish(config_.aggregations[i].kind));
            }
            writer.write_row(row);
            ++result.output_rows;
        }
    }
    writer.flush();
    return result;
}

ProcessingResult process_csv(const std::filesystem::path& input,
                             const std::filesystem::path& output, PipelineConfig config) {
    return Pipeline(std::move(config)).run(input, output);
}

} // namespace datapipe
