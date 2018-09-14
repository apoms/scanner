from scannerpy import Database, DeviceType, Job
from scannerpy.stdlib import pipelines
import subprocess
import cv2
import sys
import os.path
sys.path.append(os.path.dirname(os.path.abspath(__file__)) + '/../..')
import util

if len(sys.argv) <= 1:
    print('Usage: main.py <video_file>')
    exit(1)

movie_path = sys.argv[1]
print('Detecting faces in movie {}'.format(movie_path))
movie_name = os.path.splitext(os.path.basename(movie_path))[0]

db = Database()
print('Ingesting video into Scanner ...')
[input_table], _ = db.ingest_videos(
    [(movie_name, movie_path)], force=True)

sampler = db.streams.All
sampler_args = {}

print('Detecting faces...')
[bboxes_table] = pipelines.detect_faces(
    db, [input_table.column('frame')],
    sampler,
    sampler_args,
    movie_name + '_bboxes')

print('Drawing faces onto video...')
frame = db.sources.FrameColumn()
sampled_frame = sampler(frame)
bboxes = db.sources.Column()
out_frame = db.ops.DrawBox(frame=sampled_frame, bboxes=bboxes)
output = db.sinks.Column(columns={'frame': out_frame})
job = Job(op_args={
    frame: input_table.column('frame'),
    sampled_frame: sampler_args,
    bboxes: bboxes_table.column('bboxes'),
    output: movie_name + '_bboxes_overlay',
})
[out_table] = db.run(output=output, jobs=[job], force=True)
out_table.column('frame').save_mp4(movie_name + '_faces')

print('Successfully generated {:s}_faces.mp4'.format(movie_name))
