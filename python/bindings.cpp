#include "datapipe/pipeline.hpp"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

PYBIND11_MODULE(datapipe_cpp, module) {
    module.doc() = "Python bindings for the datapipe C++ engine";

    py::enum_<datapipe::DataType>(module, "DataType")
        .value("INTEGER", datapipe::DataType::integer)
        .value("FLOATING", datapipe::DataType::floating)
        .value("STRING", datapipe::DataType::string)
        .value("BOOLEAN", datapipe::DataType::boolean);
    py::enum_<datapipe::CompareOp>(module, "CompareOp")
        .value("EQUAL", datapipe::CompareOp::equal)
        .value("NOT_EQUAL", datapipe::CompareOp::not_equal)
        .value("LESS", datapipe::CompareOp::less)
        .value("LESS_EQUAL", datapipe::CompareOp::less_equal)
        .value("GREATER", datapipe::CompareOp::greater)
        .value("GREATER_EQUAL", datapipe::CompareOp::greater_equal);
    py::enum_<datapipe::AggregateKind>(module, "AggregateKind")
        .value("COUNT", datapipe::AggregateKind::count)
        .value("SUM", datapipe::AggregateKind::sum)
        .value("MINIMUM", datapipe::AggregateKind::minimum)
        .value("MAXIMUM", datapipe::AggregateKind::maximum)
        .value("MEAN", datapipe::AggregateKind::mean);

    py::class_<datapipe::Field>(module, "Field")
        .def(py::init<std::string, datapipe::DataType, bool>(), py::arg("name"), py::arg("type"),
             py::arg("nullable") = true)
        .def_readwrite("name", &datapipe::Field::name)
        .def_readwrite("type", &datapipe::Field::type)
        .def_readwrite("nullable", &datapipe::Field::nullable);
    py::class_<datapipe::Schema>(module, "Schema")
        .def(py::init<std::vector<datapipe::Field>>())
        .def_property_readonly("fields",
                               [](const datapipe::Schema& schema) { return schema.fields(); });
    py::class_<datapipe::Filter>(module, "Filter")
        .def(py::init<>())
        .def_readwrite("column", &datapipe::Filter::column)
        .def_readwrite("operation", &datapipe::Filter::operation)
        .def_readwrite("literal", &datapipe::Filter::literal);
    py::class_<datapipe::Aggregation>(module, "Aggregation")
        .def(py::init<>())
        .def_readwrite("kind", &datapipe::Aggregation::kind)
        .def_readwrite("column", &datapipe::Aggregation::column);
    py::class_<datapipe::PipelineConfig>(module, "PipelineConfig")
        .def(py::init<>())
        .def_readwrite("schema", &datapipe::PipelineConfig::schema)
        .def_readwrite("filter", &datapipe::PipelineConfig::filter)
        .def_readwrite("select", &datapipe::PipelineConfig::select)
        .def_readwrite("group_by", &datapipe::PipelineConfig::group_by)
        .def_readwrite("aggregations", &datapipe::PipelineConfig::aggregations)
        .def_readwrite("chunk_size", &datapipe::PipelineConfig::chunk_size)
        .def_readwrite("threads", &datapipe::PipelineConfig::threads);
    py::class_<datapipe::ProcessingResult>(module, "ProcessingResult")
        .def_readonly("input_rows", &datapipe::ProcessingResult::input_rows)
        .def_readonly("output_rows", &datapipe::ProcessingResult::output_rows)
        .def_readonly("chunks", &datapipe::ProcessingResult::chunks)
        .def_property_readonly("output_path", [](const datapipe::ProcessingResult& result) {
            return result.output_path.string();
        });

    module.def("parse_filter", &datapipe::parse_filter);
    module.def("parse_aggregations", &datapipe::parse_aggregations);
    module.def("parse_schema", &datapipe::parse_schema);
    module.def(
        "process_csv",
        [](const std::string& input, const std::string& output, datapipe::PipelineConfig config) {
            py::gil_scoped_release release;
            return datapipe::process_csv(input, output, std::move(config));
        },
        py::arg("input"), py::arg("output"), py::arg("config") = datapipe::PipelineConfig{});
}
