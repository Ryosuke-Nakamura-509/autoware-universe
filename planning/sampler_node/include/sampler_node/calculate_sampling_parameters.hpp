// Copyright 2022 Tier IV, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SAMPLER_NODE__CALCULATE_SAMPLING_PARAMETERS_HPP_
#define SAMPLER_NODE__CALCULATE_SAMPLING_PARAMETERS_HPP_

#include "frenet_planner/structures.hpp"
#include "sampler_common/structures.hpp"
#include "sampler_common/transform/spline_transform.hpp"
#include "sampler_node/parameters.hpp"

#include <motion_common/trajectory_common.hpp>

#include <autoware_auto_planning_msgs/msg/path.hpp>

#include <cmath>
#include <limits>

struct VelocityExtremum
{
  double min = std::numeric_limits<double>::max();
  double min_s{};
  double max = std::numeric_limits<double>::min();
  double max_s{};
};

VelocityExtremum findVelocityExtremum(
  const autoware_auto_planning_msgs::msg::Path & path,
  const double max_distance = std::numeric_limits<double>::max())
{
  VelocityExtremum extremum;
  if (path.points.empty()) return extremum;
  extremum.min = path.points.front().longitudinal_velocity_mps;
  extremum.max = path.points.front().longitudinal_velocity_mps;
  auto arc_length = 0.0;
  for (size_t i = 1; i < path.points.size(); ++i) {
    const auto vel = static_cast<double>(path.points[i].longitudinal_velocity_mps);
    if (vel < extremum.min) {
      extremum.min = vel;
      extremum.min_s = arc_length;
    } else if (vel > extremum.max) {
      extremum.max = vel;
      extremum.max_s = arc_length;
    }
    arc_length += autoware::common::geometry::distance_2d(
      path.points[i - 1].pose.position, path.points[i].pose.position);
    if (arc_length > max_distance) break;
  }
  return extremum;
}

inline double distanceBetweenVelocities(
  const double from_velocity, const double to_velocity, const double acceleration)
{
  return std::abs((to_velocity * to_velocity - from_velocity * from_velocity) / 2 * acceleration);
}

inline double durationBetweenVelocities(
  const double from_velocity, const double to_velocity, const double acceleration)
{
  return std::abs((to_velocity - from_velocity) / acceleration);
}

void gridSamplingParameters(
  frenet_planner::SamplingParameters & sampling_parameters, const double min_velocity,
  const double max_velocity, const double min_duration, const double max_duration,
  const int velocity_samples, const int duration_samples)
{
  sampling_parameters.target_longitudinal_velocities.reserve(velocity_samples);
  sampling_parameters.target_durations.reserve(duration_samples);
  const auto vel_step = (max_velocity - min_velocity) / velocity_samples;
  const auto dur_step = (max_duration - min_duration) / duration_samples;
  for (auto vel = min_velocity; vel < max_velocity; vel += vel_step)
    sampling_parameters.target_longitudinal_velocities.push_back(vel);
  sampling_parameters.target_longitudinal_velocities.push_back(max_velocity);
  for (auto duration = min_duration; duration < max_duration; duration += dur_step)
    sampling_parameters.target_durations.push_back(duration);
  sampling_parameters.target_durations.push_back(max_duration);
}

void calculateLongitudinalTargets(
  frenet_planner::SamplingParameters & sampling_parameters,
  const sampler_common::Configuration & initial_configuration,
  const autoware_auto_planning_msgs::msg::Path & path,
  const sampler_common::transform::Spline2D & path_spline, const Parameters & params,
  const double base_length, const double base_duration)
{
  const auto start_s = path_spline.frenet(initial_configuration.pose).s;
  const auto final_s =
    path_spline.frenet({path.points.back().pose.position.x, path.points.back().pose.position.y}).s;
  if (params.sampling.frenet.manual) {
    for (const auto d : params.sampling.frenet.target_durations) {
      if (d - base_duration > 0.0)
        sampling_parameters.target_durations.push_back(d - base_duration);
    }
    for (const auto offset : params.sampling.frenet.target_longitudinal_position_offsets) {
      if (offset - base_length > 0.0) {
        const auto target_s = start_s + offset - base_length;
        if (target_s <= final_s)
          sampling_parameters.target_longitudinal_positions.push_back(target_s);
      }
    }
    sampling_parameters.target_longitudinal_velocities =
      params.sampling.frenet.target_longitudinal_velocities;
    sampling_parameters.target_longitudinal_accelerations =
      params.sampling.frenet.target_longitudinal_accelerations;
  } else {
    const auto velocity_extremum = findVelocityExtremum(path, 50.0);
    // sampling_parameters.target_longitudinal_positions.push_back(velocity_extremum.max_s -
    // start_s);
    sampling_parameters.target_longitudinal_accelerations = {0.0};
    // sampling_parameters.target_longitudinal_velocities = {initial_configuration.velocity,
    // velocity_extremum.min, velocity_extremum.max};
    auto target_vel = 0.0;
    auto target_s = 0.0;
    if (velocity_extremum.min > 0) {
      target_vel = velocity_extremum.min;
      target_s = velocity_extremum.min_s;
    } else {
      target_vel = velocity_extremum.max;
      target_s = velocity_extremum.max_s;
    }
    if (target_s == 0.0) target_s += params.sampling.target_lengths.front();
    const auto distance = target_s - start_s;
    const auto confortable_target_vel = std::sqrt(
      initial_configuration.velocity * initial_configuration.velocity +
      2 * params.sampling.confortable_acceleration * distance);
    target_vel = std::min(target_vel, confortable_target_vel);
    const auto duration = (2 * distance) / (velocity_extremum.max - initial_configuration.velocity);
    gridSamplingParameters(
      sampling_parameters, 0.0, velocity_extremum.max, duration / 2, 4 * duration, 10, 10);
  }
}

#endif  // SAMPLER_NODE__CALCULATE_SAMPLING_PARAMETERS_HPP_
