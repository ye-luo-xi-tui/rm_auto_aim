// Copyright 2022 Chen Jun

#include "armor_processor/tracker.hpp"

#include <cfloat>
#include <iostream>
#include <memory>

namespace rm_auto_aim
{
Tracker::Tracker(double max_match_distance, int tracking_threshold, int lost_threshold)
: tracker_state(NO_FOUND),
  max_match_distance_(max_match_distance),
  tracking_threshold_(tracking_threshold),
  lost_threshold_(lost_threshold)
{
}

void Tracker::init(const Armors::SharedPtr & armors_msg, const KalmanFilterMatrices & kf_matrices)
{
  if (armors_msg->armors.empty()) {
    return;
  }

  // TODO(chenjun): need more judgement
  // Simply choose the armor that is closest to image center
  double min_distance = DBL_MAX;
  auto chosen_armor = armors_msg->armors[0];
  for (const auto & armor : armors_msg->armors) {
    if (armor.distance_to_image_center < min_distance) {
      min_distance = armor.distance_to_image_center;
      chosen_armor = armor;
    }
  }

  // KF init
  kf_ = std::make_unique<KalmanFilter>(kf_matrices);
  Eigen::VectorXd init_state(6);
  const auto position = chosen_armor.position;
  init_state << position.x, position.y, position.z, 0, 0, 0;
  kf_->init(init_state);

  tracker_state = DETECTING;
}

void Tracker::update(const Armors::SharedPtr & armors_msg, const Eigen::MatrixXd & kf_a)
{
  // KF predict
  Eigen::VectorXd kf_prediction = kf_->predict(kf_a);
  auto predicted_position = kf_prediction.head(3);

  bool matched = false;

  if (!armors_msg->armors.empty()) {
    double position_diff;
    double min_position_diff = DBL_MAX;
    auto matched_armor = armors_msg->armors[0];
    for (const auto & armor : armors_msg->armors) {
      // Difference of the current armor position and tracked armor's predicted position
      Eigen::Vector3d position_vec(armor.position.x, armor.position.y, armor.position.z);
      position_diff = (predicted_position - position_vec).norm();
      if (position_diff < min_position_diff) {
        min_position_diff = position_diff;
        matched_armor = armor;
      }
    }

    matched = min_position_diff < max_match_distance_;
    if (matched) {
      Eigen::Vector3d position_vec(
        matched_armor.position.x, matched_armor.position.y, matched_armor.position.z);
      target_state = kf_->update(position_vec);
    } else {
      target_state = kf_prediction;
    }
  }

  // Tracking state machine
  if (tracker_state == DETECTING) {
    // DETECTING
    if (matched) {
      detect_count_++;
      if (detect_count_ > tracking_threshold_) {
        detect_count_ = 0;
        tracker_state = TRACKING;
      }
    } else {
      detect_count_ = 0;
      tracker_state = NO_FOUND;
    }

  } else if (tracker_state == TRACKING) {
    // TRACKING
    if (!matched) {
      tracker_state = LOST;
      lost_count_++;
    }

  } else if (tracker_state == LOST) {
    // LOST
    if (!matched) {
      lost_count_++;
      if (lost_count_ > lost_threshold_) {
        lost_count_ = 0;
        tracker_state = NO_FOUND;
      }
    } else {
      tracker_state = TRACKING;
      lost_count_ = 0;
    }
  }
}

}  // namespace rm_auto_aim
