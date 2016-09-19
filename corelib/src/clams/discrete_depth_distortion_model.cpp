/*
Copyright (c) 2013, Alex Teichman and Stephen Miller (Stanford University)
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the <organization> nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

RTAB-Map integration: Mathieu Labbe
*/

#include "rtabmap/core/clams/discrete_depth_distortion_model.h"
#include "rtabmap/core/clams/frame_projector.h"
#include <rtabmap/utilite/ULogger.h>
#include "eigen_extensions/eigen_extensions.h"

using namespace std;
using namespace Eigen;

namespace clams
{

  DiscreteFrustum::DiscreteFrustum(int smoothing, double bin_depth) :
    max_dist_(10),
    bin_depth_(bin_depth)
  {
    num_bins_ = ceil(max_dist_ / bin_depth_);
    counts_ = VectorXf::Ones(num_bins_) * smoothing;
    total_numerators_ = VectorXf::Ones(num_bins_) * smoothing;
    total_denominators_ = VectorXf::Ones(num_bins_) * smoothing;
    multipliers_ = VectorXf::Ones(num_bins_);
  }

  void DiscreteFrustum::addExample(double ground_truth, double measurement)
  {
    double mult = ground_truth / measurement;
    if(mult > MAX_MULT || mult < MIN_MULT)
      return;
  
    int idx = min(num_bins_ - 1, (int)floor(measurement / bin_depth_));
    UASSERT(idx >= 0);

    total_numerators_(idx) += ground_truth * ground_truth;
    total_denominators_(idx) += ground_truth * measurement;
    ++counts_(idx);
    multipliers_(idx) = total_numerators_(idx) / total_denominators_(idx);
  }

  inline int DiscreteFrustum::index(double z) const
  {
    return min(num_bins_ - 1, (int)floor(z / bin_depth_));
  }
  
  inline void DiscreteFrustum::undistort(double* z) const
  {
    *z *= multipliers_.coeffRef(index(*z));
  }

  void DiscreteFrustum::interpolatedUndistort(double* z) const
  {
    int idx = index(*z);
    double start = bin_depth_ * idx;
    int idx1;
    if(*z - start < bin_depth_ / 2)
      idx1 = idx;
    else
      idx1 = idx + 1;
    int idx0 = idx1 - 1;
    if(idx0 < 0 || idx1 >= num_bins_ || counts_(idx0) < 50 || counts_(idx1) < 50) {
      undistort(z);
      return;
    }

    double z0 = (idx0 + 1) * bin_depth_ - bin_depth_ * 0.5;
    double coeff1 = (*z - z0) / bin_depth_;
    double coeff0 = 1.0 - coeff1;
    double mult = coeff0 * multipliers_.coeffRef(idx0) + coeff1 * multipliers_.coeffRef(idx1);
    *z *= mult;
  }

  void DiscreteFrustum::serialize(std::ostream& out) const
    {
      eigen_extensions::serializeScalar(max_dist_, out);
      eigen_extensions::serializeScalar(num_bins_, out);
      eigen_extensions::serializeScalar(bin_depth_, out);
      eigen_extensions::serialize(counts_, out);
      eigen_extensions::serialize(total_numerators_, out);
      eigen_extensions::serialize(total_denominators_, out);
      eigen_extensions::serialize(multipliers_, out);
    }

    void DiscreteFrustum::deserialize(std::istream& in)
    {
      eigen_extensions::deserializeScalar(in, &max_dist_);
      eigen_extensions::deserializeScalar(in, &num_bins_);
      eigen_extensions::deserializeScalar(in, &bin_depth_);
      eigen_extensions::deserialize(in, &counts_);
      eigen_extensions::deserialize(in, &total_numerators_);
      eigen_extensions::deserialize(in, &total_denominators_);
      eigen_extensions::deserialize(in, &multipliers_);
    }

  DiscreteDepthDistortionModel::DiscreteDepthDistortionModel(const DiscreteDepthDistortionModel& other)
  {
    *this = other;
  }

  DiscreteDepthDistortionModel& DiscreteDepthDistortionModel::operator=(const DiscreteDepthDistortionModel& other)
  {
    width_ = other.width_;
    height_ = other.height_;
    bin_width_ = other.bin_width_;
    bin_height_ = other.bin_height_;
    bin_depth_ = other.bin_depth_;
    num_bins_x_ = other.num_bins_x_;
    num_bins_y_ = other.num_bins_y_;
    training_samples_ = other.training_samples_;
  
    frustums_ = other.frustums_;
    for(size_t i = 0; i < frustums_.size(); ++i)
      for(size_t j = 0; j < frustums_[i].size(); ++j)
        frustums_[i][j] = new DiscreteFrustum(*other.frustums_[i][j]);

    return *this;
  }

  DiscreteDepthDistortionModel::DiscreteDepthDistortionModel(int width, int height,
                                                             int bin_width, int bin_height,
                                                             double bin_depth,
                                                             int smoothing) :
    width_(width),
    height_(height),
    bin_width_(bin_width),
    bin_height_(bin_height),
    bin_depth_(bin_depth)
  {
    UASSERT(width_ % bin_width_ == 0);
    UASSERT(height_ % bin_height_ == 0);

    num_bins_x_ = width_ / bin_width_;
    num_bins_y_ = height_ / bin_height_;
  
    frustums_.resize(num_bins_y_);
    for(size_t i = 0; i < frustums_.size(); ++i) {
      frustums_[i].resize(num_bins_x_, NULL);
      for(size_t j = 0; j < frustums_[i].size(); ++j)
        frustums_[i][j] = new DiscreteFrustum(smoothing, bin_depth);
    }

    training_samples_ = 0;
  }

  void DiscreteDepthDistortionModel::deleteFrustums()
  {
    for(size_t y = 0; y < frustums_.size(); ++y)
      for(size_t x = 0; x < frustums_[y].size(); ++x)
        if(frustums_[y][x])
          delete frustums_[y][x];
    training_samples_ = 0;
  }

  DiscreteDepthDistortionModel::~DiscreteDepthDistortionModel()
  {
    deleteFrustums();
  }

  void DiscreteDepthDistortionModel::undistort(cv::Mat & depth) const
  {
    UASSERT(width_ == depth.cols);
    UASSERT(height_ ==depth.rows);
    UASSERT(depth.type() == CV_16UC1 || depth.type() == CV_32FC1);

    if(depth.type() == CV_32FC1)
    {
		#pragma omp parallel for
		for(int v = 0; v < height_; ++v) {
		  for(int u = 0; u < width_; ++u) {
			 float & z = depth.at<float>(v, u);
			if(z == 0.0f)
			  continue;
			double zf = z;
			frustum(v, u).interpolatedUndistort(&zf);
			z = zf;
		  }
		}
    }
    else
    {
		#pragma omp parallel for
		for(int v = 0; v < height_; ++v) {
		  for(int u = 0; u < width_; ++u) {
		    unsigned short & z = depth.at<unsigned short>(v, u);
			if(z == 0)
			  continue;
			double zf = z * 0.001;
			frustum(v, u).interpolatedUndistort(&zf);
			z = zf*1000;
		  }
		}
    }
  }

  void DiscreteDepthDistortionModel::addExample(int v, int u, double ground_truth, double measurement)
  {
    frustum(v, u).addExample(ground_truth, measurement);
  }

  size_t DiscreteDepthDistortionModel::accumulate(const cv::Mat& ground_truth,
                                                  const cv::Mat& measurement)
  {
    UASSERT(width_ == ground_truth.cols);
    UASSERT(height_ == ground_truth.rows);
    UASSERT(width_ == measurement.cols);
    UASSERT(height_ == measurement.rows);
    UASSERT(ground_truth.type() == CV_16UC1 || ground_truth.type() == CV_32FC1);
    UASSERT(measurement.type() == CV_16UC1 || measurement.type() == CV_32FC1);

    bool isGroundTruthInMM = ground_truth.type()==CV_16UC1;
    bool isMeasurementInMM = measurement.type()==CV_16UC1;

    size_t num_training_examples = 0;
    for(int v = 0; v < height_; ++v) {
      for(int u = 0; u < width_; ++u) {
    	  float gt = isGroundTruthInMM?float(ground_truth.at<unsigned short>(v,u))*0.001:ground_truth.at<float>(v,u);
        if(gt == 0)
          continue;
        float meas = isMeasurementInMM?float(measurement.at<unsigned short>(v,u))*0.001:measurement.at<float>(v,u);
        if(meas == 0)
          continue;

        UScopeMutex sm(mutex_);
        frustum(v, u).addExample(gt, meas);
        ++num_training_examples;
      }
    }

    training_samples_ += num_training_examples;

    return num_training_examples;
  }

  void DiscreteDepthDistortionModel::load(const std::string& path)
    {
      ifstream f;
      f.open(path.c_str());
      if(!f.is_open()) {
        cerr << "Failed to open " << path << endl;
        assert(f.is_open());
      }
      deserialize(f);
      f.close();
    }

    void DiscreteDepthDistortionModel::save(const std::string& path) const
    {
      ofstream f;
      f.open(path.c_str());
      if(!f.is_open()) {
        cerr << "Failed to open " << path << endl;
        assert(f.is_open());
      }
      serialize(f);
      f.close();
    }

    void DiscreteDepthDistortionModel::serialize(std::ostream& out) const
    {
      out << "DiscreteDepthDistortionModel v01" << endl;
      eigen_extensions::serializeScalar(width_, out);
      eigen_extensions::serializeScalar(height_, out);
      eigen_extensions::serializeScalar(bin_width_, out);
      eigen_extensions::serializeScalar(bin_height_, out);
      eigen_extensions::serializeScalar(bin_depth_, out);
      eigen_extensions::serializeScalar(num_bins_x_, out);
      eigen_extensions::serializeScalar(num_bins_y_, out);
      eigen_extensions::serializeScalar(training_samples_, out);

      for(int y = 0; y < num_bins_y_; ++y)
        for(int x = 0; x < num_bins_x_; ++x)
          frustums_[y][x]->serialize(out);
    }

    void DiscreteDepthDistortionModel::deserialize(std::istream& in)
    {
      string buf;
      getline(in, buf);
      assert(buf == "DiscreteDepthDistortionModel v01");
      eigen_extensions::deserializeScalar(in, &width_);
      eigen_extensions::deserializeScalar(in, &height_);
      eigen_extensions::deserializeScalar(in, &bin_width_);
      eigen_extensions::deserializeScalar(in, &bin_height_);
      eigen_extensions::deserializeScalar(in, &bin_depth_);
      eigen_extensions::deserializeScalar(in, &num_bins_x_);
      eigen_extensions::deserializeScalar(in, &num_bins_y_);
      eigen_extensions::deserializeScalar(in, &training_samples_);

      deleteFrustums();
      frustums_.resize(num_bins_y_);
      for(size_t y = 0; y < frustums_.size(); ++y) {
        frustums_[y].resize(num_bins_x_, NULL);
        for(size_t x = 0; x < frustums_[y].size(); ++x) {
          frustums_[y][x] = new DiscreteFrustum;
          frustums_[y][x]->deserialize(in);
        }
      }
    }

  DiscreteFrustum& DiscreteDepthDistortionModel::frustum(int y, int x)
  {
    UASSERT(x >= 0 && x < width_);
    UASSERT(y >= 0 && y < height_);
    int xidx = x / bin_width_;
    int yidx = y / bin_height_;
    return (*frustums_[yidx][xidx]);
  }

  const DiscreteFrustum& DiscreteDepthDistortionModel::frustum(int y, int x) const
  {
    UASSERT(x >= 0 && x < width_);
    UASSERT(y >= 0 && y < height_);
    int xidx = x / bin_width_;
    int yidx = y / bin_height_;
    return (*frustums_[yidx][xidx]);
  }

}  // namespace clams
