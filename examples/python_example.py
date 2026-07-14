from datapipe_cpp import PipelineConfig, parse_aggregations, parse_filter, process_csv

config = PipelineConfig()
config.filter = parse_filter("temperature > 20")
config.group_by = "region"
config.aggregations = parse_aggregations("mean:temperature,count:*")
config.chunk_size = 50_000
config.threads = 4

result = process_csv("readings.csv", "summary.csv", config)
print(f"read {result.input_rows} rows and wrote {result.output_rows}")

