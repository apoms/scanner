
#include "scanner/api/frame.h"
#include "scanner/util/memory.h"

namespace scanner {

size_t size_of_frame_type(FrameType type) {
  size_t s;
  switch (type) {
    case FrameType::U8:
      s = sizeof(u8);
      break;
    case FrameType::F32:
      s = sizeof(f32);
      break;
    case FrameType::F64:
      s = sizeof(f64);
      break;
  }
  return s;
}

FrameInfo::FrameInfo(int shape0, int shape1, int shape2, FrameType t) {
  assert(shape0 >= 0);
  assert(shape1 >= 0);
  assert(shape2 >= 0);

  shape[0] = shape0;
  shape[1] = shape1;
  shape[2] = shape2;
  type = t;
}

FrameInfo::FrameInfo(const std::vector<int> shapes, FrameType t) {
  assert(shapes.size() <= 3);

  for (int i = 0; i < shapes.size(); ++i) {
    shape[i] = shapes[i];
    assert(shape[i] >= 0);
  }
  type = t;
}

bool FrameInfo::operator==(const FrameInfo& other) const {
  bool same = (type == other.type);
  for (int i = 0; i < FRAME_DIMS; ++i) {
    same &= (shape[i] == other.shape[i]);
  }
  return same;
}

bool FrameInfo::operator!=(const FrameInfo& other) const {
  return !(*this == other);
}

size_t FrameInfo::size() const {
  size_t s = size_of_frame_type(type);
  for (int i = 0; i < FRAME_DIMS; ++i) {
    s *= shape[i];
  }
  return s;
}

int FrameInfo::width() const { return shape[1]; }

int FrameInfo::height() const { return shape[0]; }

//! Only valid when the dimensions are (height, width, channels)
int FrameInfo::channels() const { return shape[2]; }

Frame::Frame(FrameInfo info, u8* b) : data(b) {
  memcpy(shape, info.shape, sizeof(int) * FRAME_DIMS);
  type = info.type;
}

FrameInfo Frame::as_frame_info() const {
  return FrameInfo(shape[0], shape[1], shape[2], type);
}

size_t Frame::size() const { return as_frame_info().size(); }

int Frame::width() const { return as_frame_info().width(); }

int Frame::height() const { return as_frame_info().height(); }

//! Only valid when the dimensions are (height, width, channels)
int Frame::channels() const { return as_frame_info().channels(); }

Frame* new_frame(DeviceHandle device, FrameInfo info) {
  u8* buffer = new_buffer(device, info.size());
  return new Frame(info, buffer);
}

std::vector<Frame*> new_frames(DeviceHandle device, FrameInfo info, i32 num) {
  u8* buffer = new_block_buffer(device, info.size() * num, num);
  std::vector<Frame*> frames;
  for (i32 i = 0; i < num; ++i) {
    frames.push_back(new Frame(info, buffer + i * info.size()));
  }
  return frames;
}
}
