#include "caffe2/operators/cross_entropy_op.h"

namespace caffe2 {

namespace {

inline float sigmoid_xent_forward(float lgt, float tgt) {
  return lgt * (tgt - (lgt >= 0)) - log(1 + exp(lgt - 2 * lgt * (lgt >= 0)));
}

inline float sigmoid_xent_backward(float lgt, float tgt) {
  return tgt - 1. / (1. + exp(-lgt));
}
}

template <>
bool LabelCrossEntropyOp<float, CPUContext>::RunOnDevice() {
  auto& X = Input(0);
  auto& label = Input(1);
  auto* Y = Output(0);
  DCHECK_EQ(X.ndim(), 2);
  int N = X.dim32(0);
  int D = X.dim32(1);
  DCHECK((label.ndim() == 1) || (label.ndim() == 2 && label.dim32(1) == 1));
  DCHECK_EQ(label.dim32(0), N);
  Y->Resize(vector<TIndex>{N});
  const auto* Xdata = X.data<float>();
  const auto* labeldata = label.data<int>();
  auto* Ydata = Y->mutable_data<float>();
  for (int i = 0; i < N; ++i) {
    CAFFE_ENFORCE(
        labeldata[i] < D,
        "Label seems incorrect: label value larger than number of classes: ",
        labeldata[i], " vs ", D);
    Ydata[i] = -log(std::max(Xdata[i * D + labeldata[i]], kLOG_THRESHOLD()));
  }
  return true;
}

template <>
bool SigmoidCrossEntropyWithLogitsOp<float, CPUContext>::RunOnDevice() {
  auto& logits = Input(0);
  auto& targets = Input(1);
  CAFFE_ENFORCE(logits.dims() == targets.dims());
  const auto inner_size = logits.ndim() > 0 ? logits.dims().back() : 1;
  const auto outer_size = logits.size() / inner_size;

  auto* out = Output(0);
  if (logits.ndim() == 0) {
    out->Resize(std::vector<TIndex>{});
  } else {
    std::vector<TIndex> dims(logits.dims().begin(), logits.dims().end() - 1);
    out->Resize(dims);
  }
  auto* out_ptr = out->mutable_data<float>();

  auto* logits_ptr = logits.data<float>();
  auto* targets_ptr = targets.data<float>();

  auto in_idx = 0;
  for (int i = 0; i < outer_size; ++i) {
    float value = 0;
    for (int j = 0; j < inner_size; ++j) {
      value += sigmoid_xent_forward(logits_ptr[in_idx], targets_ptr[in_idx]);
      ++in_idx;
    }
    out_ptr[i] = -value / inner_size;
  }
  return true;
}

template <>
bool SigmoidCrossEntropyWithLogitsGradientOp<float, CPUContext>::RunOnDevice() {
  auto& g = Input(0);
  auto& logits = Input(1);
  auto& targets = Input(2);
  CAFFE_ENFORCE(logits.dims() == targets.dims());
  const auto inner_size = logits.ndim() > 0 ? logits.dims().back() : 1;
  const auto outer_size = logits.size() / inner_size;
  CAFFE_ENFORCE(g.size() == outer_size);

  auto* out = Output(0);
  out->ResizeLike(logits);
  auto* out_ptr = out->mutable_data<float>();

  auto* logits_ptr = logits.data<float>();
  auto* targets_ptr = targets.data<float>();
  auto* g_ptr = g.data<float>();

  auto in_idx = 0;
  for (int i = 0; i < outer_size; ++i) {
    auto g_factor = -g_ptr[i] / inner_size;
    for (int i = 0; i < inner_size; ++i) {
      out_ptr[in_idx] = g_factor *
          sigmoid_xent_backward(logits_ptr[in_idx], targets_ptr[in_idx]);
      ++in_idx;
    }
  }
  return true;
}

template <>
bool LabelCrossEntropyGradientOp<float, CPUContext>::RunOnDevice() {
  auto& X = Input(0);
  auto& label = Input(1);
  auto& dY = Input(2);
  auto* dX = Output(0);
  DCHECK_EQ(X.ndim(), 2);
  int N = X.dim32(0);
  int D = X.dim32(1);
  DCHECK((label.ndim() == 1) || (label.ndim() == 2 && label.dim32(1) == 1));
  DCHECK_EQ(label.dim32(0), N);
  DCHECK_EQ(dY.ndim(), 1);
  DCHECK_EQ(dY.dim32(0), N);
  dX->ResizeLike(X);
  math::Set<float, CPUContext>(dX->size(), 0.f, dX->mutable_data<float>(),
                               &context_);
  const float* Xdata = X.data<float>();
  const float* dYdata = dY.data<float>();
  const int* labeldata = label.data<int>();
  float* dXdata = dX->mutable_data<float>();
  for (int i = 0; i < N; ++i) {
    dXdata[i * D + labeldata[i]] =
        - dYdata[i] / std::max(Xdata[i * D + labeldata[i]], kLOG_THRESHOLD());
  }
  return true;
}

template <>
bool MakeTwoClassOp<float, CPUContext>::RunOnDevice() {
  auto& X = Input(0);
  auto* Y = Output(0);
  auto shape = X.dims();
  shape.push_back(2);
  TIndex N = X.size();
  Y->Resize(shape);
  const auto* Xdata = X.data<float>();
  auto* Ydata = Y->mutable_data<float>();
  for (TIndex i = 0; i < N; ++i) {
    DCHECK_GE(Xdata[i], 0.0);
    DCHECK_LE(Xdata[i], 1.0);
    Ydata[i * 2] = 1.0 - Xdata[i];
    Ydata[i * 2 + 1] = Xdata[i];
  }
  return true;
}

template <>
bool MakeTwoClassGradientOp<float, CPUContext>::RunOnDevice() {
  auto& dY = Input(0);
  auto* dX = Output(0);
  auto shape = dY.dims();
  CHECK_GE(shape.size(), 1);
  CHECK_EQ(shape.back(), 2);
  shape.pop_back();
  dX->Resize(shape);
  const float* dYdata = dY.data<float>();
  float* dXdata = dX->mutable_data<float>();
  TIndex N = dX->size();
  // use eigen?
  for (TIndex i = 0; i < N; ++i) {
    dXdata[i] = dYdata[i * 2 + 1] - dYdata[i * 2];
  }
  return true;
}

namespace {
REGISTER_CPU_OPERATOR(LabelCrossEntropy,
                      LabelCrossEntropyOp<float, CPUContext>);
REGISTER_CPU_OPERATOR(LabelCrossEntropyGradient,
                      LabelCrossEntropyGradientOp<float, CPUContext>);

OPERATOR_SCHEMA(LabelCrossEntropy)
  .NumInputs(2)
  .NumOutputs(1)
  .SetDoc(R"DOC(
Operator computes the cross entropy between the input and the label set. In
practice, it is most commonly used at the end of models, after the SoftMax
operator and before the AveragedLoss operator.
  )DOC")
  .Input(0, "X", "Input blob from the previous layer, which is almost always "
  "the result of a softmax operation.")
  .Input(1, "label", "Blob containing the labels used to compare the input")
  .Output(0, "Y", "Output blob after the cross entropy computation");
OPERATOR_SCHEMA(LabelCrossEntropyGradient)
  .NumInputs(3)
  .NumOutputs(1);

class GetLabelCrossEntropyGradient : public GradientMakerBase {
  using GradientMakerBase::GradientMakerBase;
  vector<OperatorDef> GetGradientDefs() override {
    return SingleGradientDef(
        "LabelCrossEntropyGradient", "",
        vector<string>{I(0), I(1), GO(0)},
        vector<string>{GI(0)});
  }
};
REGISTER_GRADIENT(LabelCrossEntropy, GetLabelCrossEntropyGradient);

REGISTER_CPU_OPERATOR(MakeTwoClass,
                      MakeTwoClassOp<float, CPUContext>);
REGISTER_CPU_OPERATOR(MakeTwoClassGradient,
                      MakeTwoClassGradientOp<float, CPUContext>);

REGISTER_CPU_OPERATOR(
    SigmoidCrossEntropyWithLogits,
    SigmoidCrossEntropyWithLogitsOp<float, CPUContext>);
REGISTER_CPU_OPERATOR(
    SigmoidCrossEntropyWithLogitsGradient,
    SigmoidCrossEntropyWithLogitsGradientOp<float, CPUContext>);

OPERATOR_SCHEMA(MakeTwoClass)
  .NumInputs(1)
  .NumOutputs(1)
  .SetDoc(R"DOC(
Given a vector of probabilities, this operator transforms this into a 2-column
matrix with complimentary probabilities for binary classification. In explicit
terms, given the vector X, the output Y is vstack(1 - X, X).
  )DOC")
  .Input(0, "X", "Input vector of probabilities")
  .Output(0, "Y", "2-column matrix with complimentary probabilities of X for "
  "binary classification");

OPERATOR_SCHEMA(MakeTwoClassGradient)
  .NumInputs(1)
  .NumOutputs(1);

OPERATOR_SCHEMA(SigmoidCrossEntropyWithLogits)
    .NumInputs(2)
    .NumOutputs(1)
    .SetDoc(R"DOC(
Given two matrices logits and targets, of same shape,
(batch_size, num_classes), computes the sigmoid cross entropy between the two.
Returns a tensor of shape (batch_size,) of losses for each example.
)DOC")
    .Input(0, "logits", "matrix of logits for each example and class.")
    .Input(1, "targets", "matrix of targets, same shape as logits.")
    .Output(0, "xentropy", "Vector with the total xentropy for each example.");

OPERATOR_SCHEMA(SigmoidCrossEntropyWithLogitsGradient)
    .NumInputs(3)
    .NumOutputs(1);

struct GetMakeTwoClassGradient : public GradientMakerBase {
  using GradientMakerBase::GradientMakerBase;
  vector<OperatorDef> GetGradientDefs() override {
    return SingleGradientDef(
        "MakeTwoClassGradient",
        "",
        vector<string>{GO(0)},
        vector<string>{GI(0)});
  }
};
REGISTER_GRADIENT(MakeTwoClass, GetMakeTwoClassGradient);

struct GetSigmoidCrossEntropyWithLogitsGradient : public GradientMakerBase {
  using GradientMakerBase::GradientMakerBase;
  vector<OperatorDef> GetGradientDefs() override {
    return SingleGradientDef(
        "SigmoidCrossEntropyWithLogitsGradient",
        "",
        vector<string>{GO(0), I(0), I(1)},
        vector<string>{GI(0)});
  }
};
REGISTER_GRADIENT(
    SigmoidCrossEntropyWithLogits,
    GetSigmoidCrossEntropyWithLogitsGradient);

}  // namespace
}  // namespace caffe2
