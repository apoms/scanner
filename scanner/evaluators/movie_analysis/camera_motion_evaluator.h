#pragma once

#include "movie_feature_evaluator.h"

#define CERES_FOUND 1

#include <opencv2/xfeatures2d.hpp>
#include <opencv2/sfm.hpp>

namespace scanner {

class CameraMotionEvaluator : public MovieFeatureEvaluator {
public:
  CameraMotionEvaluator();

  void evaluate(
    std::vector<cv::Mat>& inputs,
    std::vector<u8*>& output_buffers,
    std::vector<size_t>& output_sizes) override;

private:
  cv::FlannBasedMatcher matcher;
  cv::Ptr<cv::xfeatures2d::SURF> detector;
};

}
