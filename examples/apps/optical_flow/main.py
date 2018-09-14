from scannerpy import Database, DeviceType, Job
import os
import os.path as osp
import numpy as np
import time
import sys

if len(sys.argv) <= 1:
    print('Usage: main.py <video_file>')
    exit(1)

video_path = sys.argv[1]
print('Performing optical flow on {}...'.format(video_path))
video_name = os.path.splitext(os.path.basename(video_path))[0]

db = Database()
if not db.has_table(video_name):
    db.ingest_videos([(video_name, video_path)])
input_table = db.table(video_name)

frame = db.sources.FrameColumn()
flow = db.ops.OpticalFlow(
    frame = frame,
    device=DeviceType.CPU)
sampled_flow = db.streams.Range(flow, 0, 60)
output = db.sinks.Column(columns={'flow': sampled_flow})

job = Job(op_args={
    frame: input_table.column('frame'),
    output: input_table.name() + '_flow'
})
[output_table] = db.run(output=output, jobs=[job],
                        pipeline_instances_per_node=1, force=True)

vid_flows = [flow[0] for flow in output_table.column('flow').load(rows=[0])]
np.save('flows.npy', vid_flows)
