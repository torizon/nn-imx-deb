/****************************************************************************
 *
 *    Copyright (c) 2020 Vivante Corporation
 *
 *    Permission is hereby granted, free of charge, to any person obtaining a
 *    copy of this software and associated documentation files (the "Software"),
 *    to deal in the Software without restriction, including without limitation
 *    the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *    and/or sell copies of the Software, and to permit persons to whom the
 *    Software is furnished to do so, subject to the following conditions:
 *
 *    The above copyright notice and this permission notice shall be included in
 *    all copies or substantial portions of the Software.
 *
 *    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *    DEALINGS IN THE SOFTWARE.
 *
 *****************************************************************************/
#include "vsi_npu_activation_op.h"
#include "vsi_npu_math_op.h"
#include "vsi_npu_norm_op.h"
#include "vsi_npu_ort_interpreter.h"
#include "vsi_npu_pool_op.h"
#include "vsi_npu_quantize_op.h"
#include "vsi_npu_reduction_op.h"
#include "vsi_npu_tensor_op.h"

namespace onnxruntime {

int VsiOrtInterpreter::run(nnrt::Model*, bool*) {
    return 0;
}

VsiOrtInterpreter::VsiOrtInterpreter() {}

void VsiOpCallbackInfo::SetupIO(nnrt::op::OperationPtr op,
                                const Node* node,
                                ModelShellPtr& model,
                                const onnxruntime::GraphViewer* graph_viewer) {
    std::vector<uint32_t> in_operand_ids;
    auto input_defs = node->InputDefs();
    for (auto input_def : input_defs) {
        uint32_t input_operand_id = model->AddOperand(input_def, graph_viewer);
        in_operand_ids.push_back(input_operand_id);
    }

    std::vector<uint32_t> out_operand_ids;
    auto output_defs = node->OutputDefs();
    for (auto output_def : output_defs) {
        uint32_t output_operand_id = model->AddOperand(output_def, graph_viewer);
        out_operand_ids.push_back(output_operand_id);
    }
    op->setInputs(in_operand_ids.data(), in_operand_ids.size());
    op->setOutputs(out_operand_ids.data(), out_operand_ids.size());
}

bool VsiOpCallbackInfo::IsNodeSupported(const onnxruntime::GraphViewer& graph_viewer,
                                        const Node* node,
                                        std::string& reason) {
    return vsi_npu::CheckAllExcludeType(node, reason) && vsi_npu::CheckAllZeroDim(node, reason);
}

void VsiOpCallbackInfo::ConstInputOprands(FunctionState state,
                                          const OrtApi* api,
                                          OrtKernelContext* context,
                                          NodeIndex node_index) {
    Ort::CustomOpApi ort{*api};
    ModelShell* model = reinterpret_cast<ModelShell*>(state);

    auto compute_info = model->GetComputeInfo(node_index);
    auto compute_input_ids = model->GetComputeInputIds(compute_info->compute_input_names);

    for (size_t i = 0; i < compute_input_ids.size(); i++) {
        model->ConstInputOprand(api, context, compute_info, compute_input_ids, i);
    }
}

void VsiOpCallbackInfoConv::SetupAttribute(nnrt::op::OperationPtr op,
                                           const Node* node,
                                           ModelShellPtr& model,
                                           const onnxruntime::GraphViewer* graph_viewer) {
    ProtoHelperNodeContext ctx(*node);
    OpNodeProtoHelper<ProtoHelperNodeContext> attrs(&ctx);

    std::vector<int32_t> vpads(4, 0);
    std::vector<int32_t> pads;
    bool status = vsi_npu::GetAttrs<int32_t>(attrs, "pads", pads, false).IsOK();

    if (status) {
        vpads[0] = pads[1];
        vpads[1] = pads[3];
        vpads[2] = pads[0];
        vpads[3] = pads[2];
    }

    std::string auto_pad;
    status = vsi_npu::GetAttr<std::string>(attrs, "auto_pad", &auto_pad).IsOK();
    nnrt::PadType pad_type = nnrt::PadType::AUTO;
    if (status) {
        pad_type = vsi_npu::GetPadType(auto_pad);
    }

    // add stride
    std::vector<int32_t> vstrides(2, 1);
    std::vector<int32_t> strides;
    status = vsi_npu::GetAttrs<int32_t>(attrs, "strides", strides, true).IsOK();
    if (status) {
        vstrides = std::move(strides);
    }

    std::vector<int32_t> vdilations(2, 1);
    std::vector<int32_t> dilations;
    status = vsi_npu::GetAttrs<int32_t>(attrs, "dilations", dilations, true).IsOK();
    if (status) {
        vdilations = std::move(dilations);
    }

    int32_t group;
    status = vsi_npu::GetAttr<int32_t>(attrs, "group", &group).IsOK();
    ORT_ENFORCE(status);

    auto conv2d_ = std::dynamic_pointer_cast<nnrt::op::GroupedConv2DOperation>(op);
    conv2d_->groups = group;
    conv2d_->pad = std::move(vpads);
    conv2d_->strides = std::move(vstrides);
    conv2d_->dilations = std::move(vdilations);
    conv2d_->padType = pad_type;
    conv2d_->setDataLayout(nnrt::DataLayout::NCHW);
    conv2d_->setVxParam(
        nnrt::OverflowPolicy::SATURATE, nnrt::RoundingPolicy::TO_ZERO, nnrt::Rounding::FLOOR);
}

bool VsiOpCallbackInfoConv::IsNodeSupported(const onnxruntime::GraphViewer& graph_viewer,
                                            const Node* node,
                                            std::string& reason) {
    auto input_defs = node->InputDefs();
    auto shape = vsi_npu::GetTensorShape(*input_defs[0]);
    if (shape.NumDimensions() != 4) {
        reason += "## Only support Conv2D now.";
        return false;
    }
    return VsiOpCallbackInfo::IsNodeSupported(graph_viewer, node, reason);
}

void VsiOpCallbackInfoSoftmax::Setup(const onnxruntime::Node* node,
                                     onnxruntime::ModelShellPtr& model,
                                     const onnxruntime::GraphViewer* graph_viewer) {
    auto softmax = std::make_shared<nnrt::op::SoftmaxOperation>();

    auto input_defs = node->InputDefs();
    auto output_defs = node->OutputDefs();
    int dim_size = input_defs[0]->Shape()->dim_size();

    ProtoHelperNodeContext ctx(*node);
    OpNodeProtoHelper<ProtoHelperNodeContext> attrs(&ctx);

    int32_t axis = 1;
    bool status = vsi_npu::GetAttr<int32_t>(attrs, "axis", &axis).IsOK();
    if (status && axis == -1) {
        axis = (axis + dim_size) % dim_size;
    }

    uint32_t front_reshape_input_id{0};
    uint32_t front_reshape_output_id{0};
    uint32_t back_reshape_input_id{0};
    uint32_t back_reshape_output_id{0};
    uint32_t softmax_input_id{0};
    uint32_t softmax_output_id{0};

    std::vector<uint32_t> softmax_in_operand_ids;
    std::vector<uint32_t> softmax_out_operand_ids;
    const std::string softmax_input_add_name = "@@softmaxinputaddTmp";
    const std::string softmax_output_add_name = "@@softmaxoutputaddTmp";

    if (dim_size == 2) {
        softmax_input_id = model->AddOperand(input_defs[0], graph_viewer);
        softmax_output_id = model->AddOperand(output_defs[0], graph_viewer);
        softmax_in_operand_ids.push_back(softmax_input_id);
        softmax_out_operand_ids.push_back(softmax_output_id);
        softmax->setInputs(softmax_in_operand_ids.data(), softmax_in_operand_ids.size());
        softmax->setOutputs(softmax_out_operand_ids.data(), softmax_out_operand_ids.size());
        softmax->axis = axis;
        model->AddOperation(softmax, nullptr);
    } else {
        front_reshape_input_id = model->AddOperand(input_defs[0], graph_viewer);
        front_reshape_output_id = model->AddOperand(input_defs[0]->Name() + softmax_input_add_name);
        back_reshape_input_id = model->AddOperand(output_defs[0]->Name() + softmax_output_add_name);
        back_reshape_output_id = model->AddOperand(output_defs[0], graph_viewer);
        softmax_input_id = front_reshape_output_id;
        softmax_output_id = back_reshape_input_id;
        const std::vector<uint32_t> front_reshape_operand_ids{front_reshape_input_id,
                                                              front_reshape_output_id};
        const std::vector<uint32_t> back_reshape_operand_ids{back_reshape_input_id,
                                                             back_reshape_output_id};
        AddReshapeOp(node, model, front_reshape_operand_ids, softmax_input_add_name, true, axis);
        softmax_in_operand_ids.push_back(softmax_input_id);
        softmax_out_operand_ids.push_back(softmax_output_id);
        softmax->setInputs(softmax_in_operand_ids.data(), softmax_in_operand_ids.size());
        softmax->setOutputs(softmax_out_operand_ids.data(), softmax_out_operand_ids.size());
        if (dim_size == 3) softmax->axis = 2;
        else softmax->axis = 1;
        model->AddOperation(softmax, nullptr);
        AddReshapeOp(node, model, back_reshape_operand_ids, softmax_output_add_name, false, axis);
    }
}

void VsiOpCallbackInfoSoftmax::AddReshapeOp(const onnxruntime::Node* node,
                                            onnxruntime::ModelShellPtr& model,
                                            const std::vector<uint32_t>& reshape_operand_ids,
                                            const std::string& reshape_add_name,
                                            bool isfront,
                                            int32_t axis) {
    auto reshape = std::make_shared<nnrt::op::ReshapeOperation>();
    auto input_defs = node->InputDefs();
    auto output_defs = node->OutputDefs();

    auto reshape_add_tensor_info = std::make_shared<VsiGraphTensorInfo>();
    if (isfront == true) {
        reshape_add_tensor_info->name = input_defs[0]->Name() + reshape_add_name;
    } else {
        reshape_add_tensor_info->name = output_defs[0]->Name() + reshape_add_name;
    }
    reshape_add_tensor_info->is_initializer = true;
    model->GetGraphInputs().push_back(reshape_add_tensor_info);

    auto shape = vsi_npu::GetTensorShape(*input_defs[0]);
    const std::vector<int64_t>& dims = shape.GetDims();
    std::vector<uint32_t> vdims;
    for (size_t i = 0; i < dims.size(); i++) {
        vdims.push_back(static_cast<uint32_t>(dims[i]));
    }

    nnrt::op::OperandPtr reshap_operand;
    if (isfront == true) {
        reshap_operand = model->GetOperand(reshape_operand_ids[1]);
    } else {
        reshap_operand = model->GetOperand(reshape_operand_ids[0]);
    }
    reshap_operand->type = nnrt::OperandType::TENSOR_FLOAT32;

    uint32_t inner_size = 1, outer_size = 1;
    if (vdims.size() == 3) {
        vdims.insert(vdims.begin(), 1);
    }

    for (uint32_t i = 0; i < static_cast<uint32_t>(axis); i++) {
        inner_size *= vdims[i];
    }

    for (uint32_t i = static_cast<uint32_t>(axis + 1); i < vdims.size(); i++) {
        outer_size *= vdims[i];
    }

    reshap_operand->dimensions.push_back(inner_size);
    reshap_operand->dimensions.push_back(vdims[axis]);
    reshap_operand->dimensions.push_back(outer_size);

    if (isfront == true) {
        std::vector<int32_t> vshapes{static_cast<int32_t>(inner_size),
                                     static_cast<int32_t>(vdims[axis]),
                                     static_cast<int32_t>(outer_size)};
        reshape->shape = std::move(vshapes);
    } else {
        std::vector<int32_t> vshapes;
        for (uint32_t i = 0; i < dims.size(); i++) {
            vshapes.push_back(static_cast<int32_t>(dims[i]));
        }
    }

    std::vector<uint32_t> reshape_in_operand_ids{reshape_operand_ids[0]};
    std::vector<uint32_t> reshape_out_operand_ids{reshape_operand_ids[1]};
    reshape->setInputs(reshape_in_operand_ids.data(), reshape_in_operand_ids.size());
    reshape->setOutputs(reshape_out_operand_ids.data(), reshape_out_operand_ids.size());
    model->AddOperation(reshape, nullptr);
}

bool VsiOpCallbackInfoSoftmax::IsNodeSupported(const onnxruntime::GraphViewer& graph_viewer,
                                               const Node* node,
                                               std::string& reason) {
    auto version_map = graph_viewer.DomainToVersionMap();
    auto version = version_map[node->Domain()];
    if (version >= 13) {
        ProtoHelperNodeContext ctx(*node);
        OpNodeProtoHelper<ProtoHelperNodeContext> attrs(&ctx);
        auto input_defs = node->InputDefs();
        auto shape = vsi_npu::GetTensorShape(*input_defs[0]);
        const std::vector<int64_t>& dims = shape.GetDims();
        int32_t axis = 1;
        vsi_npu::GetAttr<int32_t>(attrs, "axis", &axis).IsOK();
        if (dims.size() == 3 && axis == 1) return false;
    }
    return VsiOpCallbackInfo::IsNodeSupported(graph_viewer, node, reason);
}

void VsiOpCallbackInfoGemm::AddMulOp(const onnxruntime::Node* node,
                                     onnxruntime::ModelShellPtr& model,
                                     std::vector<uint32_t> mul_operand_ids,
                                     std::vector<std::string> mul_add_names,
                                     uint32_t num) {
    auto input_defs = node->InputDefs();
    auto output_defs = node->OutputDefs();
    auto mul = std::make_shared<nnrt::op::MulOperation>();

    std::vector<uint32_t> mul_in_operand_ids{mul_operand_ids[0], mul_operand_ids[1]};
    std::vector<uint32_t> mul_out_operand_ids{mul_operand_ids[2]};

    auto mul_input_tensor_info = std::make_shared<VsiGraphTensorInfo>();
    mul_input_tensor_info->name = input_defs[num]->Name() + mul_add_names[0 + num * 2];
    mul_input_tensor_info->is_initializer = true;
    model->GetGraphInputs().push_back(mul_input_tensor_info);
    model->GetGraphOutputs().push_back(mul_input_tensor_info);

    auto mul_output_tensor_info = std::make_shared<VsiGraphTensorInfo>();
    mul_output_tensor_info->name = input_defs[num]->Name() + mul_add_names[1 + num * 2];
    mul_output_tensor_info->is_initializer = true;
    model->GetGraphInputs().push_back(mul_output_tensor_info);

    auto mul_intput_operand_a = model->GetOperand(mul_operand_ids[0]);
    if (mul_intput_operand_a->ndim() == 0) {
        mul_intput_operand_a->type = nnrt::OperandType::TENSOR_FLOAT32;
        auto intput0_shape = vsi_npu::GetTensorShape(*input_defs[0]);
        auto intput1_shape = vsi_npu::GetTensorShape(*input_defs[1]);
        const std::vector<int64_t>& intput0_dims = intput0_shape.GetDims();
        const std::vector<int64_t>& intput1_dims = intput1_shape.GetDims();
        mul_intput_operand_a->dimensions.push_back(static_cast<uint32_t>(intput0_dims[0]));
        mul_intput_operand_a->dimensions.push_back(static_cast<uint32_t>(intput1_dims[1]));
    }
    auto mul_intput_operand_b = model->GetOperand(mul_operand_ids[1]);
    if (mul_intput_operand_b->ndim() == 0) {
        mul_intput_operand_b->type = nnrt::OperandType::TENSOR_FLOAT32;
        if (num == 1) {
            vsi_npu::SetTensorDims(*input_defs[2], mul_intput_operand_b->dimensions);
        } else if (num == 0) {
            auto intput0_shape = vsi_npu::GetTensorShape(*input_defs[0]);
            auto intput1_shape = vsi_npu::GetTensorShape(*input_defs[1]);
            const std::vector<int64_t>& intput0_dims = intput0_shape.GetDims();
            const std::vector<int64_t>& intput1_dims = intput1_shape.GetDims();
            mul_intput_operand_b->dimensions.push_back(static_cast<uint32_t>(intput0_dims[0]));
            mul_intput_operand_b->dimensions.push_back(static_cast<uint32_t>(intput1_dims[1]));
        }
    }

    auto mul_output_operand = model->GetOperand(mul_operand_ids[2]);
    if (mul_output_operand->ndim() == 0) {
        mul_output_operand->type = nnrt::OperandType::TENSOR_FLOAT32;
        vsi_npu::SetTensorDims(*output_defs[0], mul_output_operand->dimensions);
    }

    ProtoHelperNodeContext ctx(*node);
    OpNodeProtoHelper<ProtoHelperNodeContext> attrs(&ctx);
    float alpha{1.0};
    float beta{1.0};
    auto status_alpha = vsi_npu::GetAttr<float>(attrs, "alpha", &alpha).IsOK();
    auto status_beta = vsi_npu::GetAttr<float>(attrs, "beta", &beta).IsOK();

    auto tensor_scale = model->GetModelPtr()->operand(mul_operand_ids[1]);
    auto tensor_size_scale = tensor_scale->size();
    auto value = new float[tensor_size_scale];

    if (num == 0 && status_alpha) {
        for (uint16_t i = 0; i < tensor_size_scale; i++) {
            value[i] = alpha;
        }

    } else if (num == 1 && status_beta) {
        for (uint16_t i = 0; i < tensor_size_scale; i++) {
            value[i] = beta;
        }
    }

    std::shared_ptr<float> tensorValue(value);
    const void* value_addr = reinterpret_cast<const void*>(tensorValue.get());
    model->GetModelPtr()->setOperandValue(mul_operand_ids[1], value_addr, tensor_scale->bytes());

    mul->setInputs(mul_in_operand_ids.data(), mul_in_operand_ids.size());
    mul->setOutputs(mul_out_operand_ids.data(), mul_out_operand_ids.size());
    model->AddOperation(mul, nullptr);
}

void VsiOpCallbackInfoGemm::AddTransposeOp(const onnxruntime::Node* node,
                                           onnxruntime::ModelShellPtr& model,
                                           std::vector<uint32_t> trans_operand_ids,
                                           std::string trans_add_name) {
    auto trans = std::make_shared<nnrt::op::PermuteOperation>();

    std::vector<uint32_t> trans_in_operand_ids{trans_operand_ids[0]};
    std::vector<uint32_t> trans_output_operand_ids{trans_operand_ids[1]};

    auto input_defs = node->InputDefs();
    auto output_defs = node->OutputDefs();

    auto trans_output_tensor_info = std::make_shared<VsiGraphTensorInfo>();
    trans_output_tensor_info->name = output_defs[0]->Name() + trans_add_name;
    trans_output_tensor_info->is_initializer = true;
    model->GetGraphInputs().push_back(trans_output_tensor_info);
    model->GetGraphOutputs().push_back(trans_output_tensor_info);

    auto trans_operand = model->GetOperand(trans_operand_ids[1]);
    if (trans_operand->ndim() == 0) {
        trans_operand->type = nnrt::OperandType::TENSOR_FLOAT32;
        auto shape = vsi_npu::GetTensorShape(*input_defs[1]);
        const std::vector<int64_t>& dims = shape.GetDims();
        trans_operand->dimensions.push_back(static_cast<uint32_t>(dims[1]));
        trans_operand->dimensions.push_back(static_cast<uint32_t>(dims[0]));
    }

    std::vector<int32_t> perm_default = {1, 0};
    trans->perm = std::move(perm_default);

    trans->setInputs(trans_in_operand_ids.data(), trans_in_operand_ids.size());
    trans->setOutputs(trans_output_operand_ids.data(), trans_output_operand_ids.size());

    model->AddOperation(trans, nullptr);
}
void VsiOpCallbackInfoGemm::AddAddOp(const onnxruntime::Node* node,
                                     onnxruntime::ModelShellPtr& model,
                                     std::vector<uint32_t> add_operand_ids,
                                     std::string add_add_name) {
    auto add = std::make_shared<nnrt::op::AddOperation>();

    std::vector<uint32_t> add_in_operand_ids{add_operand_ids[0], add_operand_ids[1]};
    std::vector<uint32_t> add_output_operand_ids{add_operand_ids[2]};

    auto output_defs = node->OutputDefs();

    auto add_output_tensor_info = std::make_shared<VsiGraphTensorInfo>();
    add_output_tensor_info->name = output_defs[0]->Name() + add_add_name;
    add_output_tensor_info->is_initializer = true;
    model->GetGraphInputs().push_back(add_output_tensor_info);
    model->GetGraphOutputs().push_back(add_output_tensor_info);

    auto add_operand_a = model->GetOperand(add_operand_ids[0]);
    if (add_operand_a->ndim() == 0) {
        add_operand_a->type = nnrt::OperandType::TENSOR_FLOAT32;
        vsi_npu::SetTensorDims(*output_defs[0], add_operand_a->dimensions);
    }

    add->setInputs(add_in_operand_ids.data(), add_in_operand_ids.size());
    add->setOutputs(add_output_operand_ids.data(), add_output_operand_ids.size());

    model->AddOperation(add, nullptr);
}
void VsiOpCallbackInfoGemm::AddMatmulOp(const onnxruntime::Node* node,
                                        onnxruntime::ModelShellPtr& model,
                                        std::vector<uint32_t> matmul_operand_ids) {
    auto matmul = std::make_shared<nnrt::op::MatrixMulOperation>();

    ProtoHelperNodeContext ctx(*node);
    OpNodeProtoHelper<ProtoHelperNodeContext> attrs(&ctx);
    int64_t transA{false};
    auto status = vsi_npu::GetAttr<int64_t>(attrs, "transA", &transA).IsOK();
    if (status && transA == 1) {
        matmul->transpose[0] = true;
    } else {
        matmul->transpose[0] = false;
    }
    matmul->transpose[1] = false;

    std::vector<uint32_t> matmul_in_operand_ids{matmul_operand_ids[0], matmul_operand_ids[1]};
    std::vector<uint32_t> matmul_out_operand_ids{matmul_operand_ids[2]};

    matmul->setInputs(matmul_in_operand_ids.data(), matmul_in_operand_ids.size());
    matmul->setOutputs(matmul_out_operand_ids.data(), matmul_out_operand_ids.size());
    matmul->setVxParam(
        nnrt::OverflowPolicy::SATURATE, nnrt::RoundingPolicy::TO_ZERO, nnrt::Rounding::FLOOR);
    model->AddOperation(matmul, nullptr);
};

void VsiOpCallbackInfoGemm::Setup(const onnxruntime::Node* node,
                                  onnxruntime::ModelShellPtr& model,
                                  const onnxruntime::GraphViewer* graph_viewer) {
    ProtoHelperNodeContext ctx(*node);
    OpNodeProtoHelper<ProtoHelperNodeContext> attrs(&ctx);

    auto input_defs = node->InputDefs();
    auto output_defs = node->OutputDefs();

    int64_t transB{0};
     vsi_npu::GetAttr<int64_t>(attrs, "transB", &transB).IsOK();
    bool is_transB = (transB == 1);

    float alpha{1.0};
    float beta{1.0};
    vsi_npu::GetAttr<float>(attrs, "alpha", &alpha).IsOK();
    vsi_npu::GetAttr<float>(attrs, "beta", &beta).IsOK();

    bool has_alpha = (alpha != 1.0);
    bool has_beta = (beta != 1.0);
    bool has_C = (input_defs.size() == 3);

    const std::string trans_b_add_name = "@@trans_addTmp";
    const std::string matmul_add_name = "@@matmul_addTmp";
    const std::string mul_a_input_add_name = "@@mul_a_input_addTmp";
    const std::string mul_a_output_add_name = "@@mul_a_output_addTmp";
    const std::string mul_b_input_add_name = "@@mul_b_input_addTmp";
    const std::string mul_b_output_add_name = "@@mul_b_output_addTmp";

    uint32_t trans_a_output_operand_id{0};
    uint32_t trans_b_input_operand_id{0};
    uint32_t trans_b_output_operand_id{0};
    uint32_t add_output_operand_id{0};
    uint32_t matmul_output_operand_id{0};
    uint32_t mul_a_input_operand_id_b{0};
    uint32_t mul_a_output_operand_id{0};
    uint32_t mul_b_input_operand_id_a{0};
    uint32_t mul_b_input_operand_id_b{0};
    uint32_t mul_b_output_operand_id{0};

    std::vector<std::string> mul_add_names{mul_a_input_add_name,
                                               mul_a_output_add_name,
                                               mul_b_input_add_name,
                                               mul_b_output_add_name};

    // NNRT support transA, but not support transB
    trans_a_output_operand_id = model->AddOperand(input_defs[0], graph_viewer);

    if (is_transB) {
        trans_b_input_operand_id = model->AddOperand(input_defs[1], graph_viewer);
        trans_b_output_operand_id = model->AddOperand(input_defs[1]->Name() + trans_b_add_name);
        std::vector<uint32_t> trans_operand_ids{trans_b_input_operand_id, trans_b_output_operand_id};
        AddTransposeOp(node, model, trans_operand_ids, trans_b_add_name);
    }
    else {
        trans_b_output_operand_id = model->AddOperand(input_defs[1], graph_viewer);
    }

    add_output_operand_id = model->AddOperand(output_defs[0], graph_viewer);
    if (has_C) {
        matmul_output_operand_id = model->AddOperand(output_defs[0]->Name() + matmul_add_name);
        std::vector<uint32_t> matmul_operand_ids{
            trans_a_output_operand_id, trans_b_output_operand_id, matmul_output_operand_id};
        AddMatmulOp(node, model, matmul_operand_ids);

        if (has_alpha) {
            mul_a_input_operand_id_b = model->AddOperand(input_defs[0]->Name() + mul_a_input_add_name);
            mul_a_output_operand_id = model->AddOperand(output_defs[0]->Name() + mul_a_output_add_name);
            std::vector<uint32_t> mul_a_operand_ids{
                matmul_output_operand_id, mul_a_input_operand_id_b, mul_a_output_operand_id};
            AddMulOp(node, model, mul_a_operand_ids, mul_add_names, 0);
        }
        else {
            mul_a_output_operand_id = matmul_output_operand_id;
        }

        if (has_beta) {
            mul_b_input_operand_id_a = model->AddOperand(input_defs[2], graph_viewer);
            mul_b_input_operand_id_b = model->AddOperand(input_defs[0]->Name() + mul_b_input_add_name);
            mul_b_output_operand_id = model->AddOperand(output_defs[0]->Name() + mul_b_output_add_name);
            std::vector<uint32_t> mul_b_operand_ids{
                mul_b_input_operand_id_a, mul_b_input_operand_id_b, mul_b_output_operand_id};
            AddMulOp(node, model, mul_b_operand_ids, mul_add_names, 1);
        }
        else {
            mul_b_output_operand_id = model->AddOperand(input_defs[2], graph_viewer);
        }

        std::vector<uint32_t> add_operand_ids{
            mul_a_output_operand_id, mul_b_output_operand_id, add_output_operand_id};
        AddAddOp(node, model, add_operand_ids, matmul_add_name);
    } else {
        if (has_alpha) {
            matmul_output_operand_id = model->AddOperand(output_defs[0]->Name() + matmul_add_name);
                std::vector<uint32_t> matmul_operand_ids{
            trans_a_output_operand_id, trans_b_output_operand_id, matmul_output_operand_id};
            AddMatmulOp(node, model, matmul_operand_ids);

            mul_a_input_operand_id_b = model->AddOperand(input_defs[0]->Name() + mul_a_input_add_name);
            std::vector<uint32_t> mul_a_operand_ids{
                matmul_output_operand_id, mul_a_input_operand_id_b, add_output_operand_id};
            AddMulOp(node, model, mul_a_operand_ids, mul_add_names, 0);
        }
        else {
            std::vector<uint32_t> matmul_operand_ids{
                trans_a_output_operand_id, trans_b_output_operand_id, add_output_operand_id};
            AddMatmulOp(node, model, matmul_operand_ids);
        }
    }
}

bool VsiOpCallbackInfoGemm::IsNodeSupported(const onnxruntime::GraphViewer& graph_viewer,
                                            const Node* node,
                                            std::string& reason) {
    if (VsiOpCallbackInfo::IsNodeSupported(graph_viewer, node, reason)) {
        auto input_defs = node->InputDefs();
        auto shape_input = vsi_npu::GetTensorShape(*input_defs[0]);
        auto shape_w = vsi_npu::GetTensorShape(*input_defs[1]);
        ProtoHelperNodeContext ctx(*node);
        OpNodeProtoHelper<ProtoHelperNodeContext> attrs(&ctx);
        int64_t transB{0};
        vsi_npu::GetAttr<int64_t>(attrs, "transB", &transB).IsOK();
        size_t count = shape_input[0] * shape_input[1];
        if (transB == 0) {
            count *= shape_w[0];
        } else {
            count *= shape_w[1];
        }
        if (count <= 1024 * 1024) return true;
    }
    return false;
}

void VsiOpCallbackInfoUpsample::Setup(const Node* node,
                                      ModelShellPtr& model,
                                      const onnxruntime::GraphViewer* graph_viewer) {
    auto input_defs = node->InputDefs();
    std::vector<uint32_t> in_operand_ids;
    uint32_t input_operand_id = model->AddOperand(input_defs[0], graph_viewer);
    in_operand_ids.push_back(input_operand_id);

    auto output_defs = node->OutputDefs();
    std::vector<uint32_t> out_operand_ids;
    uint32_t output_operand_id = model->AddOperand(output_defs[0], graph_viewer);
    out_operand_ids.push_back(output_operand_id);

    ProtoHelperNodeContext ctx(*node);
    OpNodeProtoHelper<ProtoHelperNodeContext> attrs(&ctx);

    std::string mode;
    bool status = vsi_npu::GetAttr<std::string>(attrs, "mode", &mode).IsOK();
    ORT_ENFORCE(status);

    auto compute_info = std::make_shared<VsiComputeInfo>();
    std::vector<float> scales;
    if (input_defs.size() > 1) {
        model->GetInitializerAsParameters<float>(input_defs[1], graph_viewer, scales);
        if (scales.size() == 0) {
            const int32_t kIndexScale = 1;
            std::vector<int32_t> attrs_index({kIndexScale});
            compute_info->backup_names.push_back(mode);
            model->CollectComputeInfo(node, graph_viewer, attrs_index, compute_info);
        }
    } else {
        bool status = vsi_npu::GetAttrs<float>(attrs, "scales", scales, false).IsOK();
        ORT_ENFORCE(status);
    }

    int32_t outputHeight = 0;
    int32_t outputWidth = 0;
    auto shape = vsi_npu::GetTensorShape(*(input_defs[0]));
    const std::vector<int64_t>& dims = shape.GetDims();
    if (scales.size() == 4) {
        outputHeight = dims[2] * scales[2];
        outputWidth = dims[3] * scales[3];
    } else if (scales.size() == 2) {
        outputHeight = dims[2] * scales[0];
        outputWidth = dims[3] * scales[1];
    } else {
        outputHeight = dims[2];
        outputWidth = dims[3];
    }

    if (mode == "nearest") {
        auto op = std::make_shared<nnrt::op::ResizeNearestNeighborOperation>();
        op->setInputs(in_operand_ids.data(), in_operand_ids.size());
        op->setOutputs(out_operand_ids.data(), out_operand_ids.size());

        op->outputHeight = outputHeight;
        op->outputWidth = outputWidth;
        model->AddOperation(op, nullptr);
        compute_info->op = op;
    } else if (mode == "linear") {
        auto op = std::make_shared<nnrt::op::ResizeBilinearOperation>();
        op->setInputs(in_operand_ids.data(), in_operand_ids.size());
        op->setOutputs(out_operand_ids.data(), out_operand_ids.size());

        op->outputHeight = outputHeight;
        op->outputWidth = outputWidth;
        model->AddOperation(op, nullptr);
        compute_info->op = op;
    }
}

Status VsiOpCallbackInfoUpsample::Compute(FunctionState state,
                                          const OrtApi* api,
                                          OrtKernelContext* context,
                                          NodeIndex node_index) {
    Ort::CustomOpApi ort{*api};
    ModelShell* model = reinterpret_cast<ModelShell*>(state);
    auto compute_info = model->GetComputeInfo(node_index);
    if (compute_info == nullptr) return Status::OK();

    auto attributes_input_ids = model->GetComputeInputIds(compute_info->compute_input_names);

    const OrtValue* input_tensor_scale =
        ort.KernelContext_GetInput(context, attributes_input_ids[0]);
    const auto input_tensor_scale_value = (float*)ort.GetTensorData<void>(input_tensor_scale);

    std::string mode = compute_info->backup_names[0];
    auto op = compute_info->op;
    if (mode == "nearest") {
        auto upsample = std::dynamic_pointer_cast<nnrt::op::ResizeNearestNeighborOperation>(op);
        upsample->outputHeight *= input_tensor_scale_value[2];
        upsample->outputWidth *= input_tensor_scale_value[3];
    } else if (mode == "linear") {
        auto upsample = std::dynamic_pointer_cast<nnrt::op::ResizeBilinearOperation>(op);
        upsample->outputHeight *= input_tensor_scale_value[2];
        upsample->outputWidth *= input_tensor_scale_value[3];
    }
    return Status::OK();
}

bool VsiOpCallbackInfoUpsample::IsNodeSupported(const onnxruntime::GraphViewer& graph_viewer,
                                                const Node* node,
                                                std::string& reason) {
    ProtoHelperNodeContext ctx(*node);
    OpNodeProtoHelper<ProtoHelperNodeContext> attrs(&ctx);

    std::vector<float> scales;
    vsi_npu::GetAttrs<float>(attrs, "scales", scales, false).IsOK();

    if (scales.size() != 4 && scales.size() != 2 && scales.size() != 0) return false;
    if (scales.size() == 4 && (scales[0] != 1 || scales[1] != 1)) return false;
    return vsi_npu::CheckMainInputType(node, reason) && vsi_npu::CheckAllZeroDim(node, reason);
}

void VsiOpCallbackInfoResize::Setup(const Node* node,
                                      ModelShellPtr& model,
                                      const onnxruntime::GraphViewer* graph_viewer) {
    auto input_defs = node->InputDefs();
    std::vector<uint32_t> in_operand_ids;
    uint32_t input_operand_id = model->AddOperand(input_defs[0], graph_viewer);
    in_operand_ids.push_back(input_operand_id);

    auto output_defs = node->OutputDefs();
    std::vector<uint32_t> out_operand_ids;
    uint32_t output_operand_id = model->AddOperand(output_defs[0], graph_viewer);
    out_operand_ids.push_back(output_operand_id);

    ProtoHelperNodeContext ctx(*node);
    OpNodeProtoHelper<ProtoHelperNodeContext> attrs(&ctx);

    std::string mode;
    bool status = vsi_npu::GetAttr<std::string>(attrs, "mode", &mode).IsOK();
    ORT_ENFORCE(status);

    auto compute_info = std::make_shared<VsiComputeInfo>();
    std::vector<float> scales;
    model->GetInitializerAsParameters<float>(input_defs[1], graph_viewer, scales);
    if (scales.size() == 0) {
        const int32_t kIndexScale = 1;
        std::vector<int32_t> attrs_index({kIndexScale});
        compute_info->backup_names.push_back(mode);
        model->CollectComputeInfo(node, graph_viewer, attrs_index, compute_info);
    }

    int32_t outputHeight = 0;
    int32_t outputWidth = 0;
    auto shape = vsi_npu::GetTensorShape(*(input_defs[0]));
    const std::vector<int64_t>& dims = shape.GetDims();
    if (scales.size() == 4) {
        outputHeight = dims[2] * scales[2];
        outputWidth = dims[3] * scales[3];
    } else if (scales.size() == 2) {
        outputHeight = dims[2] * scales[0];
        outputWidth = dims[3] * scales[1];
    } else {
        outputHeight = dims[2];
        outputWidth = dims[3];
    }

    if (mode == "nearest") {
        auto op = std::make_shared<nnrt::op::ResizeNearestNeighborOperation>();
        op->setInputs(in_operand_ids.data(), in_operand_ids.size());
        op->setOutputs(out_operand_ids.data(), out_operand_ids.size());

        op->outputHeight = outputHeight;
        op->outputWidth = outputWidth;
        op->align_corners = false;
        op->half_pixel_centers = false;
        model->AddOperation(op, nullptr);
        compute_info->op = op;
    } else if (mode == "linear") {
        auto op = std::make_shared<nnrt::op::ResizeBilinearOperation>();
        op->setInputs(in_operand_ids.data(), in_operand_ids.size());
        op->setOutputs(out_operand_ids.data(), out_operand_ids.size());

        op->outputHeight = outputHeight;
        op->outputWidth = outputWidth;
        op->align_corners = false;
        op->half_pixel_centers = false;
        model->AddOperation(op, nullptr);
        compute_info->op = op;
    }
}

Status VsiOpCallbackInfoResize::Compute(FunctionState state,
                                          const OrtApi* api,
                                          OrtKernelContext* context,
                                          NodeIndex node_index) {
    Ort::CustomOpApi ort{*api};
    ModelShell* model = reinterpret_cast<ModelShell*>(state);
    auto compute_info = model->GetComputeInfo(node_index);
    if (compute_info == nullptr) return Status::OK();

    auto attributes_input_ids = model->GetComputeInputIds(compute_info->compute_input_names);

    const OrtValue* input_tensor_scale =
        ort.KernelContext_GetInput(context, attributes_input_ids[0]);
    const auto input_tensor_scale_value = (float*)ort.GetTensorData<void>(input_tensor_scale);

    std::string mode = compute_info->backup_names[0];
    auto op = compute_info->op;
    if (mode == "nearest") {
        auto resize = std::dynamic_pointer_cast<nnrt::op::ResizeNearestNeighborOperation>(op);
        resize->outputHeight *= input_tensor_scale_value[2];
        resize->outputWidth *= input_tensor_scale_value[3];
    } else if (mode == "linear") {
        auto resize = std::dynamic_pointer_cast<nnrt::op::ResizeBilinearOperation>(op);
        resize->outputHeight *= input_tensor_scale_value[2];
        resize->outputWidth *= input_tensor_scale_value[3];
    }

    return Status::OK();
}

bool VsiOpCallbackInfoResize::IsNodeSupported(const onnxruntime::GraphViewer& graph_viewer,
                                                const Node* node,
                                                std::string& reason) {
    auto input_defs = node->InputDefs();
    auto output_defs = node->OutputDefs();
    ModelShellPtr model = std::make_shared<ModelShell>();
    std::vector<float> scales;
    model->GetInitializerAsParameters<float>(input_defs[1], &graph_viewer, scales);

    if (scales.size() != 4 && scales.size() != 2) return false;
    if (scales.size() == 4 && (scales[0] != 1 || scales[1] != 1)) return false;
    if (!(vsi_npu::CheckMainInputType(node, reason) &&
          vsi_npu::CheckAllZeroDim(node, reason))) return false;
    float scale_h = 1.0, scale_w = 1.0;
    float scale_h0 = 1.0, scale_w0 = 1.0;
    auto input_shape = vsi_npu::GetTensorShape(*input_defs[0]);
    auto output_shape = vsi_npu::GetTensorShape(*output_defs[0]);
    if (scales.size() == 4) {
        scale_h = scales[2];
        scale_w = scales[3];
        scale_h0 = (float)(output_shape[2]) / input_shape[2];
        scale_w0 = (float)(output_shape[3]) / input_shape[3];
    } else { // scales.size() == 2
        scale_h = scales[0];
        scale_w = scales[1];
        scale_h0 = (float)(output_shape[0]) / input_shape[0];
        scale_w0 = (float)(output_shape[1]) / input_shape[1];
    }

    if (fabs(scale_h - scale_h0) > 1e-6 || fabs(scale_w - scale_w0) > 1e-6) return false;

    return true;
}

#define REGISTER_OP(name)                               \
    std::pair<std::string, std::shared_ptr<VsiOpInfo>>( \
        #name, std::dynamic_pointer_cast<VsiOpInfo>(std::make_shared<VsiOpInfo##name>()))

std::map<std::string, std::shared_ptr<VsiOpInfo>> vsi_npu_supported_ops = {
    REGISTER_OP(Relu),
    REGISTER_OP(Abs),
    REGISTER_OP(Add),
    REGISTER_OP(Sub),
    REGISTER_OP(Mul),
    REGISTER_OP(Div),
    REGISTER_OP(Sum),
    REGISTER_OP(Conv),
    REGISTER_OP(Concat),
    REGISTER_OP(MaxPool),
    REGISTER_OP(AveragePool),
    REGISTER_OP(GlobalMaxPool),
    REGISTER_OP(GlobalAveragePool),
    REGISTER_OP(Softmax),
    REGISTER_OP(Reshape),
    REGISTER_OP(Gemm),
    REGISTER_OP(Transpose),
    REGISTER_OP(LRN),
    REGISTER_OP(DequantizeLinear),
    REGISTER_OP(QuantizeLinear),
    REGISTER_OP(LeakyRelu),
    REGISTER_OP(Upsample),
    REGISTER_OP(InstanceNormalization),
    REGISTER_OP(Pad),
    REGISTER_OP(BatchNormalization),
    REGISTER_OP(ConvInteger),
    REGISTER_OP(MatMul),
    REGISTER_OP(QLinearConv),
    REGISTER_OP(Sigmoid),
    REGISTER_OP(Sqrt),
    REGISTER_OP(Tanh),
    REGISTER_OP(Log),
    REGISTER_OP(Pow),
    REGISTER_OP(Exp),
    REGISTER_OP(ArgMax),
    REGISTER_OP(ReduceMean),
    REGISTER_OP(Clip),
    REGISTER_OP(Resize),
};

bool VsiSupported(const std::string& opName) {
    return vsi_npu_supported_ops.find(opName) != vsi_npu_supported_ops.end();
}

std::shared_ptr<VsiOpInfo> getVsiFunc(const std::string& opName) {
    return vsi_npu_supported_ops[opName];
}
}  // namespace onnxruntime
