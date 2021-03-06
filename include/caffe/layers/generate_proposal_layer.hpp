#ifndef CAFFE_BBOX_TRANSFORM_LAYER_HPP_
#define CAFFE_BBOX_TRANSFORM_LAYER_HPP_

#include <vector>

#include "caffe/blob.hpp"
#include "caffe/common.hpp"
#include "caffe/layer.hpp"
#include "caffe/proto/caffe.pb.h"

#include <cstring>

#include "caffe2/utils/eigen_utils.h"
#include "caffe2/utils/generate_proposals_op_util_nms.h"

#define DEBUG 0
namespace caffe {  


namespace utils {

  // A sub tensor view
  template <class T>
  class ConstTensorView {
   public:
    ConstTensorView(const T* data, const std::vector<int>& dims)
        : data_(data), dims_(dims) {}

    int ndim() const {
      return dims_.size();
    }
    const std::vector<int>& dims() const {
      return dims_;
    }
    int dims(int i) const {
      //caffe2::DCHECK_LE(i, dims_.size());
      return dims_[i];
    }
    int dim(int i) const {
      //caffe2::DCHECK_LE(i, dims_.size());
      return dims_[i];
    }
    const T* data() const {
      return data_;
    }
    size_t size() const {
      return std::accumulate(
          dims_.begin(), dims_.end(), 1, std::multiplies<size_t>());
    }

   private:
    const T* data_ = nullptr;
    std::vector<int> dims_;
  };

  // Generate a list of bounding box shapes for each pixel based on predefined
  //     bounding box shapes 'anchors'.
  // anchors: predefined anchors, size(A, 4)
  // Return: all_anchors_vec: (H * W, A * 4)
  // Need to reshape to (H * W * A, 4) to match the format in python
  caffe2::ERMatXf ComputeAllAnchors(
      const float* anchors,
      const int A, const int box_dim,
      int height,
      int width,
      float feat_stride);

  size_t size_from_dim_(size_t k, const std::vector<int>& dims) {
    size_t r = 1;
    for (size_t i = k; i < dims.size(); ++i) {
      r *= dims[i];
    }
    return r;
  }

  // Compute the 1-d index of a n-dimensional contiguous row-major tensor for
  //     a given n-dimensional index 'index'
  size_t ComputeStartIndex(
      const std::vector<int>& tensor_shape,
      const std::vector<int>& index) {
    //DCHECK_EQ(index.size(), tensor.ndim());
    
    size_t ret = 0;
  if(DEBUG){
      printf("ComputeStartIndex starts\n");
      for (int i = 0; i < index.size(); i++) {
        ret += index[i] * size_from_dim_(i + 1, tensor_shape);
        printf("ret : %d index[i] %d size_from_dim_(%d, tensor_shape): %d \n", ret, index[i],i+1, size_from_dim_(i + 1, tensor_shape));
      }
  }    
    return ret;
  }

  // Get a sub tensor view from 'tensor' using data pointer from 'tensor'
  template <class T>
  ConstTensorView<T> GetSubTensorView(
      const float* tensor,
      const std::vector<int>& tensor_shape,
      int tensor_count,
      int dim0_start_index) {
    //DCHECK_EQ(tensor.meta().itemsize(), sizeof(T));

    if (tensor_shape.size() == 0) {
      return ConstTensorView<T>(nullptr, {});
    }
    //std::vector<int> start_dims(tensor.ndim(), 0);
    std::vector<int> start_dims(tensor_shape.size(), 0);//tensor_shape.size() == 3 if 3D or 4 if 4D, size()== number of axis
    start_dims.at(0) = dim0_start_index;
    //auto st_idx = ComputeStartIndex(tensor, start_dims);
    auto st_idx = ComputeStartIndex(tensor_shape, start_dims);
    //auto ptr = tensor.data<T>() + st_idx;
    
    auto ptr = tensor + st_idx;

    //auto& input_dims = tensor.dims(); // 1-D int vector like (1,3,244,244)
    //std::vector<int> ret_dims(input_dims.begin() + 1, input_dims.end());
    std::vector<int> ret_dims(tensor_shape.begin() + 1, tensor_shape.end());// changed input_dims->tensor_shape
    if(DEBUG){
      printf("st_idx : %d\n",st_idx);
      for (int i = 0; i < ret_dims.size(); i++) { 
        printf("ret_dims.shape(%d) : %d \n",i, ret_dims[i]);
      }
    }
    ConstTensorView<T> ret(ptr, ret_dims);
    return ret;
  }

  caffe2::ERMatXf ComputeAllAnchors(
      const float* anchors,
      const int A, const int box_dim,
      int height,
      int width,
      float feat_stride) {

    const auto K = height * width;
    //CAFFE_ENFORCE(box_dim == 4 || box_dim == 5);
    caffe2::ERMatXf shift_x = (caffe2::ERVecXf::LinSpaced(width, 0.0, width - 1.0) * feat_stride)
                          .replicate(height, 1);
    caffe2::ERMatXf shift_y = (caffe2::EVecXf::LinSpaced(height, 0.0, height - 1.0) * feat_stride)
                          .replicate(1, width);
    Eigen::MatrixXf shifts(K, box_dim);
    if (box_dim == 4) {
      // Upright boxes in [x1, y1, x2, y2] format
      shifts << caffe2::ConstEigenVectorMap<float>(shift_x.data(), shift_x.size()),
          caffe2::ConstEigenVectorMap<float>(shift_y.data(), shift_y.size()),
          caffe2::ConstEigenVectorMap<float>(shift_x.data(), shift_x.size()),
          caffe2::ConstEigenVectorMap<float>(shift_y.data(), shift_y.size());
    } else {
      // Rotated boxes in [ctr_x, ctr_y, w, h, angle] format.
      // Zero shift for width, height and angle.
      caffe2::ERMatXf shift_zero = caffe2::ERMatXf::Constant(height, width, 0.0);
      shifts << caffe2::ConstEigenVectorMap<float>(shift_x.data(), shift_x.size()),
          caffe2::ConstEigenVectorMap<float>(shift_y.data(), shift_y.size()),
          caffe2::ConstEigenVectorMap<float>(shift_zero.data(), shift_zero.size()),
          caffe2::ConstEigenVectorMap<float>(shift_zero.data(), shift_zero.size()),
          caffe2::ConstEigenVectorMap<float>(shift_zero.data(), shift_zero.size());
    } 
    // Broacast anchors over shifts to enumerate all anchors at all positions
    // in the (H, W) grid:
    //   - add A anchors of shape (1, A, box_dim) to
    //   - K shifts of shape (K, 1, box_dim) to get
    //   - all shifted anchors of shape (K, A, box_dim)
    //   - reshape to (K*A, box_dim) shifted anchors
    caffe2::ConstEigenMatrixMap<float> anchors_vec(anchors, 1, A * box_dim); 
    // equivalent to python code
    //  all_anchors = (
    //        self._model.anchors.reshape((1, A, box_dim)) +
    //        shifts.reshape((1, K, box_dim)).transpose((1, 0, 2)))
    //    all_anchors = all_anchors.reshape((K * A, box_dim))
    // all_anchors_vec: (K, A * box_dim)
    caffe2::ERMatXf all_anchors_vec =
        anchors_vec.replicate(K, 1) + shifts.rowwise().replicate(A); 
    // use the following to reshape to (K * A, box_dim)
    // Eigen::Map<const caffe2::ERMatXf> all_anchors(
    //            all_anchors_vec.data(), K * A, box_dim); 
    return all_anchors_vec;
  }

} // namespace utils


template <typename Dtype>
class GenerateProposalLayer : public Layer<Dtype> {
 public:

  explicit GenerateProposalLayer(const LayerParameter& param)
      : Layer<Dtype>(param) {}
  virtual void LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top); 
  virtual void Reshape(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top); 
  void ProposalsForOneImage(
      const Eigen::Array3f& im_info,
      const Eigen::Map<const caffe2::ERMatXf>& all_anchors,
      const utils::ConstTensorView<float>& bbox_deltas_tensor,
      const utils::ConstTensorView<float>& scores_tensor,
      caffe2::ERArrXXf* out_boxes,
      caffe2::EArrXf* out_probs) const;
 protected:
  virtual void Forward_cpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Backward_cpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom){
  NOT_IMPLEMENTED;}
 // spatial_scale_ must be declared before feat_stride_
  float spatial_scale_=1.0;
  float feat_stride_= 1.0;
  float nms_thresh_ = 0.699999988079071;// RPN_NMS_THRESH
  int pre_nms_topn_=6000; // RPN_PRE_NMS_TOP_N
  float min_size_  =0.0;
  int post_nms_topn_=300;// RPN_POST_NMS_TOP_N
  int correct_transform_coords_=1;
  // RPN_MIN_SIZE
  float rpn_min_size_=16;
  // Correct bounding box transform coordates, see bbox_transform() in boxes.py
  // Set to true to match the detectron code, set to false for backward
  // compatibility
  //bool correct_transform_coords_=false;
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
