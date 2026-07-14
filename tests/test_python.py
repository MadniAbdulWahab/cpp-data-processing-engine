import sys
from pathlib import Path

module_dir, input_path, output_path = map(Path, sys.argv[1:])
sys.path.insert(0, str(module_dir))

import datapipe_cpp

config = datapipe_cpp.PipelineConfig()
config.filter = datapipe_cpp.parse_filter("temperature > 20")
config.group_by = "region"
config.aggregations = datapipe_cpp.parse_aggregations("mean:temperature,count:*")
config.chunk_size = 2
config.threads = 3

result = datapipe_cpp.process_csv(str(input_path), str(output_path), config)
assert result.input_rows == 5
assert result.output_rows == 2
assert result.chunks == 3
assert output_path.read_text(encoding="utf-8").splitlines() == [
    "region,mean_temperature,count",
    "east,20.5,1",
    "north,22.75,2",
]

