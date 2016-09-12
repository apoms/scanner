/* Copyright 2016 Carnegie Mellon University
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "scanner/util/profiler.h"
#include "scanner/util/common.h"

namespace scanner {

enum class VideoDecoderType {
  NVIDIA,
  INTEL,
  SOFTWARE,
};

///////////////////////////////////////////////////////////////////////////////
/// VideoDecoder
class VideoDecoder {
public:
  static std::vector<VideoDecoderType> get_supported_decoder_types();

  static bool has_decoder_type(VideoDecoderType type);

  static VideoDecoder* make_from_config(
    DeviceType device_type,
    int device_id,
    VideoDecoderType type,
    DatasetItemMetadata metadata);

  virtual ~VideoDecoder() {};

  virtual bool feed(
    const char* encoded_buffer,
    size_t encoded_size,
    bool discontinuity = false) = 0;

  virtual bool discard_frame() = 0;

  virtual bool get_frame(
    char* decoded_buffer,
    size_t decoded_size) = 0;

  virtual int decoded_frames_buffered() = 0;

  virtual void wait_until_frames_copied() = 0;

  void set_profiler(Profiler* profiler);

private:
  Profiler* profiler_ = nullptr;
};

}