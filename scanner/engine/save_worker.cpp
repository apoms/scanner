
#include "scanner/engine/save_worker.h"

#include "scanner/engine/metadata.h"
#include "scanner/util/common.h"
#include "scanner/util/storehouse.h"
#include "scanner/video/h264_byte_stream_index_creator.h"

#include "storehouse/storage_backend.h"

#include <glog/logging.h>

using storehouse::StoreResult;
using storehouse::WriteFile;
using storehouse::RandomReadFile;

namespace scanner {
namespace internal {

SaveWorker::SaveWorker(const SaveWorkerArgs& args)
    : node_id_(args.node_id), worker_id_(args.worker_id), profiler_(args.profiler) {
  auto setup_start = now();
  // Setup a distinct storage backend for each IO thread
  storage_.reset(
      storehouse::StorageBackend::make_from_config(args.storage_config));

  args.profiler.add_interval("setup", setup_start, now());

}

SaveWorker::~SaveWorker() {
  for (auto& file : output_) {
    BACKOFF_FAIL(file->save());
  }
  for (auto& file : output_metadata_) {
    BACKOFF_FAIL(file->save());
  }
  for (auto& meta : video_metadata_) {
    write_video_metadata(storage_.get(), meta);
  }
  output_.clear();
  output_metadata_.clear();
  video_metadata_.clear();
}

void SaveWorker::feed(EvalWorkEntry& input_entry) {
  EvalWorkEntry& work_entry = input_entry;

  // Write out each output column to an individual data file
  i32 video_col_idx = 0;
  for (size_t out_idx = 0; out_idx < work_entry.columns.size(); ++out_idx) {
    u64 num_elements = static_cast<u64>(work_entry.columns[out_idx].size());

    auto io_start = now();

    WriteFile* output_file = output_.at(out_idx).get();
    WriteFile* output_metadata_file = output_metadata_.at(out_idx).get();

    if (work_entry.columns[out_idx].size() != num_elements) {
      LOG(FATAL) << "Output layer's element vector has wrong length";
    }

    // Ensure the data is on the CPU
    move_if_different_address_space(profiler_,
                                    work_entry.column_handles[out_idx],
                                    CPU_DEVICE, work_entry.columns[out_idx]);

    bool compressed = work_entry.compressed[out_idx];
    // If this is a video...
    i64 size_written = 0;
    if (work_entry.column_types[out_idx] == ColumnType::Video) {
      // Read frame info column
      assert(work_entry.columns[out_idx].size() > 0);
      FrameInfo frame_info = work_entry.frame_sizes[video_col_idx];

      // Create index column
      VideoMetadata& video_meta = video_metadata_[video_col_idx];
      proto::VideoDescriptor& video_descriptor = video_meta.get_descriptor();

      video_descriptor.set_width(frame_info.width());
      video_descriptor.set_height(frame_info.height());
      video_descriptor.set_channels(frame_info.channels());
      video_descriptor.set_frame_type(frame_info.type);

      video_descriptor.set_time_base_num(1);
      video_descriptor.set_time_base_denom(25);

      video_descriptor.set_num_encoded_videos(
          video_descriptor.num_encoded_videos() + 1);

      if (compressed && frame_info.type == FrameType::U8 &&
          frame_info.channels() == 3) {
        H264ByteStreamIndexCreator index_creator(output_file);
        for (size_t i = 0; i < num_elements; ++i) {
          Element& element = work_entry.columns[out_idx][i];
          if (!index_creator.feed_packet(element.buffer, element.size)) {
            LOG(FATAL) << "Error in save worker h264 index creator: "
                       << index_creator.error_message();
          }
          size_written += element.size;
        }

        i64 frame = index_creator.frames();
        i32 num_non_ref_frames = index_creator.num_non_ref_frames();
        const std::vector<u8>& metadata_bytes = index_creator.metadata_bytes();
        const std::vector<u64>& keyframe_indices =
            index_creator.keyframe_indices();
        const std::vector<u64>& sample_offsets =
            index_creator.sample_offsets();
        const std::vector<u64>& sample_sizes =
            index_creator.sample_sizes();

        video_descriptor.set_chroma_format(proto::VideoDescriptor::YUV_420);
        video_descriptor.set_codec_type(proto::VideoDescriptor::H264);

        video_descriptor.set_frames(video_descriptor.frames() + frame);
        video_descriptor.add_frames_per_video(frame);
        video_descriptor.add_keyframes_per_video(keyframe_indices.size());
        video_descriptor.add_size_per_video(index_creator.bytestream_pos());
        video_descriptor.set_metadata_packets(metadata_bytes.data(),
                                              metadata_bytes.size());

        const std::string output_path =
            table_item_output_path(video_descriptor.table_id(), out_idx,
                                   video_descriptor.item_id());
        video_descriptor.set_data_path(output_path);
        video_descriptor.set_inplace(false);

        for (u64 v : keyframe_indices) {
          video_descriptor.add_keyframe_indices(v);
        }
        for (u64 v : sample_offsets) {
          video_descriptor.add_sample_offsets(v);
        }
        for (u64 v : sample_sizes) {
          video_descriptor.add_sample_sizes(v);
        }
      } else {
        // Non h264 compressible video column
        video_descriptor.set_codec_type(proto::VideoDescriptor::RAW);
        // Need to specify but not used for this type
        video_descriptor.set_chroma_format(proto::VideoDescriptor::YUV_420);
        video_descriptor.set_frames(video_descriptor.frames() + num_elements);

        // Write number of elements in the file
        s_write(output_metadata_file, num_elements);
        // Write out all output sizes first so we can easily index into the
        // file
        for (size_t i = 0; i < num_elements; ++i) {
          Frame* frame = work_entry.columns[out_idx][i].as_frame();
          u64 buffer_size = frame->size();
          s_write(output_metadata_file, buffer_size);
          size_written += sizeof(u64);
        }
        // Write actual output data
        for (size_t i = 0; i < num_elements; ++i) {
          Frame* frame = work_entry.columns[out_idx][i].as_frame();
          i64 buffer_size = frame->size();
          u8* buffer = frame->data;
          s_write(output_file, buffer, buffer_size);
          size_written += buffer_size;
        }
      }

      video_col_idx++;
    } else {
      // Write number of elements in the file
      s_write(output_metadata_file, num_elements);
      // Write out all output sizes to metadata file  so we can easily index into the data file
      for (size_t i = 0; i < num_elements; ++i) {
        u64 buffer_size = work_entry.columns[out_idx][i].size;
        s_write(output_metadata_file, buffer_size);
        size_written += sizeof(u64);
      }
      // Write actual output data
      for (size_t i = 0; i < num_elements; ++i) {
        i64 buffer_size = work_entry.columns[out_idx][i].size;
        u8* buffer = work_entry.columns[out_idx][i].buffer;
        s_write(output_file, buffer, buffer_size);
        size_written += buffer_size;
      }
    }

    // TODO: For now, all evaluators are expected to return CPU
    //   buffers as output so just assume CPU
    for (size_t i = 0; i < num_elements; ++i) {
      delete_element(CPU_DEVICE, work_entry.columns[out_idx][i]);
    }

    profiler_.add_interval("io", io_start, now());
    profiler_.increment("io_write", size_written);
  }
}

void SaveWorker::new_task(i32 table_id, i32 task_id,
                          std::vector<ColumnType> column_types) {
  auto io_start = now();
  for (auto& file : output_) {
    BACKOFF_FAIL(file->save());
  }
  for (auto& file : output_metadata_) {
    BACKOFF_FAIL(file->save());
  }
  for (auto& meta : video_metadata_) {
    write_video_metadata(storage_.get(), meta);
  }
  output_.clear();
  output_metadata_.clear();
  video_metadata_.clear();

  profiler_.add_interval("io", io_start, now());

  for (size_t out_idx = 0; out_idx < column_types.size(); ++out_idx) {
    const std::string output_path =
        table_item_output_path(table_id, out_idx, task_id);
    const std::string output_metdata_path =
        table_item_metadata_path(table_id, out_idx, task_id);

    WriteFile* output_file = nullptr;
    BACKOFF_FAIL(storage_->make_write_file(output_path, output_file));
    output_.emplace_back(output_file);

    WriteFile* output_metadata_file = nullptr;
    BACKOFF_FAIL(
        storage_->make_write_file(output_metdata_path, output_metadata_file));
    output_metadata_.emplace_back(output_metadata_file);

    if (column_types[out_idx] == ColumnType::Video) {
      video_metadata_.emplace_back();

      VideoMetadata& video_meta = video_metadata_.back();
      proto::VideoDescriptor& video_descriptor = video_meta.get_descriptor();
      video_descriptor.set_table_id(table_id);
      video_descriptor.set_column_id(out_idx);
      video_descriptor.set_item_id(task_id);
    }
  }
}
}
}
