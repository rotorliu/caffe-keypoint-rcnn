#ifndef CAFFE_BBOX_TRANSFORM_LAYER_HPP_
#define CAFFE_BBOX_TRANSFORM_LAYER_HPP_

#include <vector>

#include "caffe/blob.hpp"
#include "caffe/common.hpp"
#include "caffe/layer.hpp"
#include "caffe/proto/caffe.pb.h"

#include <cstring>
#include "caffe2/utils/eigen_utils.h"
namespace caffe {
 
template <typename Dtype>
class BBoxTransformLayer : public Layer<Dtype> {
 public:
  explicit BBoxTransformLayer(const LayerParameter& param)
      : Layer<Dtype>(param) {}
  virtual void LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Reshape(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);


 protected:
  virtual void Forward_cpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Backward_cpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom){
  NOT_IMPLEMENTED;} 
  // weights [wx, wy, ww, wh] to apply to the regression target
  float weights_1_=10.0;
  float weights_2_=10.0;
  float weights_3_=5.0;
  float weights_4_=5.0;
  // Transform the boxes to the scaled image space after applying the bbox
  //   deltas.
  // Set to false to match the detectron code, set to true for the keypoint
  //   model and for backward compatibility
  bool apply_scale_=true;
  // Correct bounding box transform coordates, see bbox_transform() in boxes.py
  // Set to true to match the detectron code, set to false for backward
  //   compatibility
  bool correct_transform_coords_=false;
  // Set for RRPN case to handle rotated boxes. Inputs should be in format
  // [ctr_x, ctr_y, width, height, angle (in degrees)].
  bool rotated_=false;
  // If set, for rotated boxes in RRPN, output angles are normalized to be
  // within [angle_bound_lo, angle_bound_hi].
  bool angle_bound_on_=true;
  int angle_bound_lo_=-90;
  int angle_bound_hi_=90;
  // For RRPN, clip almost horizontal boxes within this threshold of
  // tolerance for backward compatibility. Set to negative value for
  // no clipping.
  float clip_angle_thresh_=1.0;
};

}  // namespace caffe

#endif  // CAFFE_BBOX_TRANSFORM_LAYER_HPP_
