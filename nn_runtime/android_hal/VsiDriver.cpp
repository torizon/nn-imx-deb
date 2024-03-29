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

#include "VsiDriver.h"

#include <openssl/md5.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <nnrt/file_map_memory.hpp>
#include <set>
#include <string>
#include <vector>

#include "VsiPlatform.h"
#include "VsiRTInfo.h"

#define MD5_SECRET_LEN_16 (16)
#define MD5_BYTE_STRING_LEN (4)

#if ANDROID_SDK_VERSION >= 29
#include "public.hpp"
#endif

namespace android {
namespace nn {
namespace vsi_driver {

#if ANDROID_SDK_VERSION >= 29
using OperationValidatePtr = std::unique_ptr<
    android::nn::op_validate::OperationValidate<HalPlatform::Model, HalPlatform::Operation>>;
#endif

void VsiDriver::initalizeEnv() {
    disable_float_feature_ = 0;

    disable_float_feature_ = getSystemPropertyAsInt("DISABLE_FLOAT_FEATURE", 0);
    if (disable_float_feature_) {
        LOG(INFO) << "Disabled float datatype on NPU by request!";
    }
}

const uint8_t* VsiDriver::getOperandDataPtr(const HalPlatform::Model& model,
                                            const HalPlatform::Operand& hal_operand,
                                            VsiRTInfo& vsiMemory) {
    if (OperandLifeTime::CONSTANT_COPY == hal_operand.lifetime) {
        return model.operandValues.data() + hal_operand.location.offset;
    } else if (OperandLifeTime::CONSTANT_REFERENCE == hal_operand.lifetime) {
        if (!mapHidlMem(model.pools[hal_operand.location.poolIndex], vsiMemory)) return nullptr;
        return vsiMemory.getPtr(hal_operand.location.offset);
    }
    return nullptr;
}

const uint8_t* getOpeandPtr(const HalPlatform::Model& model,
                            const HalPlatform::Operand& operand,
                            struct VsiRTInfo& rt) {
    auto& location = operand.location;
    if (operand.lifetime == OperandLifeTime::CONSTANT_COPY) {
        return model.operandValues.data() + location.offset;
    } else if (operand.lifetime == OperandLifeTime::CONSTANT_REFERENCE) {
        return VsiDriver::getOperandDataPtr(model, operand, rt);
    } else
        return nullptr;
};

template <typename T_type>
T_type getScalarData(const HalPlatform::Model& model, const HalPlatform::Operand& operand) {
    struct VsiRTInfo rt;
    auto ptr = getOpeandPtr(model, operand, rt);
    if (ptr)
        return *reinterpret_cast<T_type*>(const_cast<uint8_t*>(ptr));
    else
        return 0;
}

bool isTensor(const HalPlatform::Operand& operand) {
    bool tensor = true;
    switch (operand.type) {
#if ANDROID_SDK_VERSION >= 29
        case OperandType::BOOL:
        case OperandType::FLOAT16:
#endif
        case OperandType::FLOAT32:
        case OperandType::INT32:
        case OperandType::UINT32:
            tensor = false;
            break;
        default:
            tensor = true;
    }
    return tensor;
}

const std::string commonMd5Secret32(const std::string& src) {
    MD5_CTX ctx;

    std::string md5String;
    unsigned char md[MD5_SECRET_LEN_16] = {0};
    char tmp[MD5_BYTE_STRING_LEN] = {0};

    MD5_Init(&ctx);
    MD5_Update(&ctx, src.c_str(), src.size());
    MD5_Final(md, &ctx);

    for (int i = 0; i < 16; ++i) {
        memset(tmp, 0x00, sizeof(tmp));
        snprintf(tmp, sizeof(tmp), "%02X", md[i]);
        md5String += tmp;
    }
    return md5String;
}

bool isConstantTensor(const HalPlatform::Operand& operand) {
    if (operand.lifetime == OperandLifeTime::CONSTANT_COPY ||
        operand.lifetime == OperandLifeTime::CONSTANT_REFERENCE)
        return true;
    else
        return false;
}

bool VsiDriver::isSupportedOperation(const HalPlatform::Operation& operation,
                                     const HalPlatform::Model& model,
                                     std::string& reason) {
    bool isSupport = true;

#if ANDROID_SDK_VERSION >= 29
    auto checkSupportedOperand = [](auto& operand) -> bool {
        bool isSupported = true;
        switch (operand.type) {
            // API 29 newly added operand
            case OperandType::BOOL:
            case OperandType::TENSOR_QUANT16_SYMM:
            case OperandType::TENSOR_FLOAT16:
            case OperandType::TENSOR_BOOL8:
            case OperandType::FLOAT16:
            case OperandType::TENSOR_QUANT8_SYMM_PER_CHANNEL:
            case OperandType::TENSOR_QUANT16_ASYMM:
            case OperandType::TENSOR_QUANT8_SYMM:
                isSupported = false;
                break;
            default:
                break;
        }
        return isSupported;
    };
#endif

    // each operation check
    switch (operation.type) {
        case OperationType::ADD:
        case OperationType::SUB:
        case OperationType::MUL:
        case OperationType::DIV: {
            // validate constant rule
            auto input0 = GetHalOperand(model, operation.inputs[0]);
            auto input1 = GetHalOperand(model, operation.inputs[1]);
            auto act    = GetHalOperand(model, operation.inputs[2]);
            // TODO{yzw}: remove the limitation when driver support int32
            if (OperandType::TENSOR_INT32 == input0.type) {
                reason += "reject ADD|SUB|MUL|DIV because not support TENSOR_INT32 temporary\n";
                isSupport &= false;
            }
            if (isConstantTensor(input0) && isConstantTensor(input1)) {
                reason += ("reject ADD|SUB|MUL|DIV because all input tensor is constant\n");
                isSupport &= false;
            }
            if (isConstantTensor(act)) {
                struct vsi_driver::VsiRTInfo rt;
                auto actCode = reinterpret_cast<const int32_t*>(getOperandDataPtr(model, act, rt));
                if (*actCode > 3) {
                    reason += ("reject ADD|SUB|MUL|DIV because fusedCode is invalid value " +
                               std::to_string(*actCode));
                    isSupport &= false;
                }
            } else {
                reason += ("reject ADD|SUB|MUL|DIV because fusedCode is not const\n");
                isSupport &= false;
            }
            // validate shape rule
            if (input0.dimensions.size() == input1.dimensions.size()) {
                for (size_t i = 0; i < input0.dimensions.size(); i++) {
                    if (input0.dimensions[i] != input1.dimensions[i]) {
                        if (input0.dimensions[i] != 1 && input1.dimensions[i] != 1) {
                            reason +=
                                "reject ADD|SUB|MUL|DIV because inputs shape is invalidated\n";
                            isSupport &= false;
                            break;
                        }
                    }
                }
            } else {
                isSupport &= true;
            }
            break;
        }
#if ANDROID_SDK_VERSION >= 29
        case OperationType::AVERAGE_POOL_2D: {
            OperationValidatePtr avgPool = std::make_unique<
                op_validate::AveragePoolValidate<HalPlatform::Model, HalPlatform::Operation>>(
                model, operation);
            return avgPool->Validate(reason);
        }
        case OperationType::MAX_POOL_2D: {
            OperationValidatePtr maxPool = std::make_unique<
                op_validate::MaxPoolValidate<HalPlatform::Model, HalPlatform::Operation>>(
                model, operation);
            return maxPool->Validate(reason);
        }
        case OperationType::L2_POOL_2D: {
            OperationValidatePtr l2Pool = std::make_unique<
                op_validate::L2PoolValidate<HalPlatform::Model, HalPlatform::Operation>>(model,
                                                                                         operation);
            return l2Pool->Validate(reason);
        }
        case OperationType::DEPTH_TO_SPACE: {
            OperationValidatePtr depth2space = std::make_unique<
                op_validate::Depth2spaceValidate<HalPlatform::Model, HalPlatform::Operation>>(
                model, operation);
            return depth2space->Validate(reason);
        }
        case OperationType::SPACE_TO_DEPTH: {
            OperationValidatePtr space2depth = std::make_unique<
                op_validate::Space2depthValidate<HalPlatform::Model, HalPlatform::Operation>>(
                model, operation);
            return space2depth->Validate(reason);
        }
        case OperationType::SPACE_TO_BATCH_ND: {
            OperationValidatePtr space2batch = std::make_unique<
                op_validate::Space2BatchValidate<HalPlatform::Model, HalPlatform::Operation>>(
                model, operation);
            return space2batch->Validate(reason);
        }
        case OperationType::BATCH_TO_SPACE_ND: {
            OperationValidatePtr batch2space = std::make_unique<
                op_validate::Batch2spaceValidate<HalPlatform::Model, HalPlatform::Operation>>(
                model, operation);
            return batch2space->Validate(reason);
        }
        case OperationType::CONV_2D: {
            OperationValidatePtr conv2D = std::make_unique<
                op_validate::Conv2dValidate<HalPlatform::Model, HalPlatform::Operation>>(model,
                                                                                         operation);
            return conv2D->Validate(reason);
        }
        case OperationType::RNN: {
            int32_t fuseCode = getScalarData<int32_t>(model, GetHalOperand(model, operation.inputs[5]));
            if (fuseCode == static_cast<int32_t>(FusedActivationFunc::RELU) ||
                fuseCode == static_cast<int32_t>(FusedActivationFunc::RELU1) ||
                fuseCode == static_cast<int32_t>(FusedActivationFunc::RELU6)) {
                isSupport &= true;
            } else {
                reason += ("reject RNN because fuseCode not support \n");
                isSupport &= false;
            }
            break;
        }

        case OperationType::SOFTMAX: {
            auto& input = GetHalOperand(model, operation.inputs[0]);
            if (isConstantTensor(input)) {
                reason += ("reject SOFTMAX because input tensor is constant\n");
                isSupport &= false;
            }
            break;
        }
        case OperationType::LSH_PROJECTION: {
            reason += ("rejcet LSH_PROJECTION because only SW implement\n");
            return false;
            // int32_t typePtr = getScalarData<int32_t>(model, model.operands[operation.inputs[3]]);
            // if (3 == typePtr) {
            //     isSupport &= false;
            // }
            // break;
        }
        case OperationType::TANH: {
            if (OperandType::TENSOR_FLOAT32 != GetHalOperand(model, operation.inputs[0]).type) {
                reason += "reject TANH because only support input_type = FLOAT32 tensor\n";
                isSupport &= false;
            }
            break;
        }
        case OperationType::LSTM: {
            if (operation.inputs.size() > 23) {
                reason += "reject LSTM because New Spec in 1.2 not enabled\n";
                isSupport &= false;
            }
            break;
        }

        case OperationType::TRANSPOSE: {
            // according to the spec, perm is optinal.
            if (operation.inputs.size() == 1) {
                reason += "reject TRANSPOSE because no perm vetor provided\n";
                isSupport &= false;
            }
            auto& perm = GetHalOperand(model, operation.inputs[1]);
            if (!isConstantTensor(perm)) {
                isSupport &= false;
                reason += "reject TRANSPOSE because permute is not constant operand \n";
            }
#if ANDROID_SDK_VERSION < 30
            if (OperandLifeTime::MODEL_INPUT == perm.lifetime) {
                isSupport &= false;
                reason += "reject TRANSPOSE because permute not supported as an input \n";
            }
#elif ANDROID_SDK_VERSION >= 30
            if (OperandLifeTime::SUBGRAPH_INPUT == perm.lifetime) {
                isSupport &= false;
                reason += "reject TRANSPOSE because permute not supported as an input \n";
            }
#endif
            size_t dimSize = perm.location.length / sizeof(int32_t);

            struct VsiRTInfo rt;
            auto permData = getOperandDataPtr(model, perm, rt);
            bool batchIsTransposed = permData && (*(int32_t*)permData != 0);
            if (dimSize > 4 || batchIsTransposed) {
                reason += "reject TRANSPOSE because >=4D or transposed on Batch not supported\n";
                isSupport &= false;
            }

            break;
        }
        case OperationType::PAD: {
            // TODO: support pad at channel and batch
            auto& pad = GetHalOperand(model, operation.inputs[1]);
            if (!isConstantTensor(pad)) return false;
            size_t dimSize = pad.dimensions[0];
            // Pad only support 4D PAD
            if (dimSize != 4) {
                reason += "reject Pad because dimSize !=4\n";
                return false;
            }
            if (dimSize < 3) {
                isSupport &= true;
                break;
            }

            struct VsiRTInfo rt;
            auto padData = reinterpret_cast<const int32_t*>(getOperandDataPtr(model, pad, rt));

            if (!padData) isSupport &= false;
            if (dimSize > 2) {
                bool noPadOn3rdDimFor3D = (dimSize == 3 && padData[4] + padData[5] != 0);
                bool noPadOn0rd3rdFor4D =
                    (dimSize == 4 && padData[6] + padData[7] + padData[0] + padData[1] == 0);
                isSupport &= (noPadOn3rdDimFor3D || noPadOn0rd3rdFor4D);
                if (!isSupport) {
                    reason += "reject PAD because pad value for 3rd or 0rd/3rd dimensions\n";
                }
            }
            break;
        }
        case OperationType::SVDF: {
            struct VsiRTInfo rtInfo;
            const auto& rankOperand = GetHalOperand(model, operation.inputs[5]);
            const int32_t* rankValue =
                reinterpret_cast<const int32_t*>(getOperandDataPtr(model, rankOperand, rtInfo));
            if (rankValue && rankValue[0] <= 2) {
                reason += "reject SVDF because rankd <= 2 not support\n";
                isSupport &= false;
            }
            break;
        }

        case OperationType::HASHTABLE_LOOKUP: {
            auto& value_tensor = GetHalOperand(model, operation.inputs[2]);
            if (2 != value_tensor.dimensions.size()) {
                reason += "reject HASHTABLE_LOOPUP until we support value tensor other than 2D\n";
                isSupport &= false;
            }
            break;
        }
        // to-do: check operand with operation
        // API 29 newly added operataion
        case OperationType::ABS: {
            OperationValidatePtr absValidate = std::make_unique<
                op_validate::AbsValidate<HalPlatform::Model, HalPlatform::Operation>>(model,
                                                                                      operation);
            return absValidate->Validate(reason);
        }

        case OperationType::ARGMAX:
        case OperationType::ARGMIN: {
            OperationValidatePtr argmaxArgmin = std::make_unique<
                op_validate::ArgmaxArgminValidate<HalPlatform::Model, HalPlatform::Operation>>(
                model, operation);
            return argmaxArgmin->Validate(reason);
        }

        case OperationType::MAXIMUM:
        case OperationType::MINIMUM: {
            OperationValidatePtr maxMin = std::make_unique<
                op_validate::MaximumMinimumValidate<HalPlatform::Model, HalPlatform::Operation>>(
                model, operation);
            return maxMin->Validate(reason);
        }

        case OperationType::RSQRT:
        case OperationType::SQRT: {
            OperationValidatePtr sqrtRsqrt = std::make_unique<
                op_validate::SqrtRsqrtValidate<HalPlatform::Model, HalPlatform::Operation>>(
                model, operation);
            return sqrtRsqrt->Validate(reason);
        }

        case OperationType::LOG: {
            OperationValidatePtr logValidate = std::make_unique<
                op_validate::LogValidate<HalPlatform::Model, HalPlatform::Operation>>(model,
                                                                                      operation);
            return logValidate->Validate(reason);
        }

        case OperationType::EXP: {
            OperationValidatePtr expValidate = std::make_unique<
                op_validate::ExpValidate<HalPlatform::Model, HalPlatform::Operation>>(model,
                                                                                      operation);
            return expValidate->Validate(reason);
        }

        case OperationType::SIN: {
            OperationValidatePtr sinValidate = std::make_unique<
                op_validate::SinValidate<HalPlatform::Model, HalPlatform::Operation>>(model,
                                                                                      operation);
            return sinValidate->Validate(reason);
        }

        case OperationType::RESIZE_BILINEAR: {
            OperationValidatePtr resizeValidate = std::make_unique<
                op_validate::ResizeValidate<HalPlatform::Model, HalPlatform::Operation>>(model,
                                                                                         operation);
            return resizeValidate->Validate(reason);
        }

        case OperationType::REDUCE_MAX: {
            OperationValidatePtr reduceMax = std::make_unique<
                op_validate::ReduceMaxValidate<HalPlatform::Model, HalPlatform::Operation>>(
                model, operation);
            return reduceMax->Validate(reason);
        }

        case OperationType::REDUCE_MIN: {
            OperationValidatePtr reduceMin = std::make_unique<
                op_validate::ReduceMinValidate<HalPlatform::Model, HalPlatform::Operation>>(
                model, operation);
            return reduceMin->Validate(reason);
        }

        case OperationType::REDUCE_PROD: {
            OperationValidatePtr reduceProd = std::make_unique<
                op_validate::ReduceProdValidate<HalPlatform::Model, HalPlatform::Operation>>(
                model, operation);
            return reduceProd->Validate(reason);
        }

        case OperationType::REDUCE_SUM: {
            OperationValidatePtr reduceSum = std::make_unique<
                op_validate::ReduceSumValidate<HalPlatform::Model, HalPlatform::Operation>>(
                model, operation);
            return reduceSum->Validate(reason);
        }

        case OperationType::NEG: {
            OperationValidatePtr neg = std::make_unique<
                op_validate::NegValidate<HalPlatform::Model, HalPlatform::Operation>>(model,
                                                                                      operation);
            return neg->Validate(reason);
        }

        case OperationType::PRELU: {
            OperationValidatePtr prelu = std::make_unique<
                op_validate::PreluValidate<HalPlatform::Model, HalPlatform::Operation>>(model,
                                                                                        operation);
            return prelu->Validate(reason);
        }

        case OperationType::LESS:
        case OperationType::LESS_EQUAL:
        case OperationType::EQUAL:
        case OperationType::GREATER:
        case OperationType::GREATER_EQUAL:
        case OperationType::NOT_EQUAL: {
            OperationValidatePtr compare = std::make_unique<
                op_validate::ComparisonValidate<HalPlatform::Model, HalPlatform::Operation>>(
                model, operation);
            return compare->Validate(reason);
        }

        case OperationType::LOGICAL_AND: {
            OperationValidatePtr logicalAnd = std::make_unique<
                op_validate::LogicalAndValidate<HalPlatform::Model, HalPlatform::Operation>>(
                model, operation);
            return logicalAnd->Validate(reason);
        }
        case OperationType::LOGICAL_NOT: {
            OperationValidatePtr logicalNot = std::make_unique<
                op_validate::LogicalNotValidate<HalPlatform::Model, HalPlatform::Operation>>(
                model, operation);
            return logicalNot->Validate(reason);
        }
        case OperationType::LOGICAL_OR: {
            OperationValidatePtr logicalOr = std::make_unique<
                op_validate::LogicalOrValidate<HalPlatform::Model, HalPlatform::Operation>>(
                model, operation);
            return logicalOr->Validate(reason);
        }
        case OperationType::EXPAND_DIMS: {
            OperationValidatePtr expandDims = std::make_unique<
                op_validate::ExpandDimsValidate<HalPlatform::Model, HalPlatform::Operation>>(
                model, operation);
            return expandDims->Validate(reason);
        }
        case OperationType::POW: {
            OperationValidatePtr pow = std::make_unique<
                op_validate::PowValidate<HalPlatform::Model, HalPlatform::Operation>>(model,
                                                                                      operation);
            return pow->Validate(reason);
        }
        case OperationType::INSTANCE_NORMALIZATION: {
            OperationValidatePtr instanceNorm = std::make_unique<
                op_validate::InstanceNormValidate<HalPlatform::Model, HalPlatform::Operation>>(
                model, operation);
            return instanceNorm->Validate(reason);
        }
        case OperationType::SPLIT: {
            OperationValidatePtr split = std::make_unique<
                op_validate::SplitValidate<HalPlatform::Model, HalPlatform::Operation>>(model,
                                                                                        operation);
            return split->Validate(reason);
        }
        case OperationType::LOG_SOFTMAX: {
            OperationValidatePtr logSoftmax = std::make_unique<
                op_validate::LogSoftmaxValidate<HalPlatform::Model, HalPlatform::Operation>>(
                model, operation);
            return logSoftmax->Validate(reason);
        }
        case OperationType::REDUCE_ALL: {
            OperationValidatePtr reduceAll = std::make_unique<
                op_validate::ReduceAllValidate<HalPlatform::Model, HalPlatform::Operation>>(
                model, operation);
            return reduceAll->Validate(reason);
        }
        case OperationType::REDUCE_ANY: {
            OperationValidatePtr reduceAny = std::make_unique<
                op_validate::ReduceAnyValidate<HalPlatform::Model, HalPlatform::Operation>>(
                model, operation);
            return reduceAny->Validate(reason);
        }
        case OperationType::GATHER: {
            OperationValidatePtr gather = std::make_unique<
                op_validate::GatherValidate<HalPlatform::Model, HalPlatform::Operation>>(model,
                                                                                         operation);
            return gather->Validate(reason);
        }

        case OperationType::AXIS_ALIGNED_BBOX_TRANSFORM: {
            OperationValidatePtr axisAlignedBBoxTransform = std::make_unique<
                op_validate::AxisAlignedBBoxTransformValidate<HalPlatform::Model,
                                                              HalPlatform::Operation>>(model,
                                                                                       operation);
            return axisAlignedBBoxTransform->Validate(reason);
        }
        case OperationType::UNIDIRECTIONAL_SEQUENCE_LSTM: {
            // All generated cases failed
            reason += "reject UNIDIRECTIONAL_SEQUENCE_LSTM \n";
            return false;
            // OperationValidatePtr unidirectionalSequenceLstm = std::make_unique<
            //    op_validate::UnidirectionalSequenceLstmValidate<HalPlatform::Model,
            //    HalPlatform::Operation>>(model,
            //                                                                          operation);

            // return unidirectionalSequenceLstm->Validate(reason);
        }
        case OperationType::BIDIRECTIONAL_SEQUENCE_LSTM: {
            // All generated cases failed, need to fix
            reason += "reject BIDIRECTIONAL_SEQUENCE_LSTM\n";
            return false;
            // OperationValidatePtr bidirectionalSequenceLstm = std::make_unique<
            //    op_validate::BidirectionalSequenceLstmValidate<HalPlatform::Model,
            //    HalPlatform::Operation>>(model,
            //                                                                          operation);
            // return bidirectionalSequenceLstm->Validate(reason);
        }
        case OperationType::GENERATE_PROPOSALS: {
            // Some generated float32 cases failed
            return false;
            // OperationValidatePtr generateProposals = std::make_unique<
            //    op_validate::GenerateProposalsValidate<HalPlatform::Model,
            //    HalPlatform::Operation>>(model,
            //                                                                          operation);
            // return generateProposals->Validate(reason);
        }
        case OperationType::DETECTION_POSTPROCESSING: {
            // Some generated float32 cases crashed
            return false;
            // OperationValidatePtr detectionPostprocessing = std::make_unique<
            //    op_validate::DetectionPostprocessingValidate<HalPlatform::Model,
            //    HalPlatform::Operation>>(model,
            //                                                                          operation);

            // return detectionPostprocessing->Validate(reason);
        }
        case OperationType::UNIDIRECTIONAL_SEQUENCE_RNN: {
            OperationValidatePtr unidirectionalSequenceRnn = std::make_unique<
                op_validate::UnidirectionalSequenceRnnValidate<HalPlatform::Model,
                                                               HalPlatform::Operation>>(model,
                                                                                        operation);
            return unidirectionalSequenceRnn->Validate(reason);
        }
        case OperationType::BIDIRECTIONAL_SEQUENCE_RNN: {
            // All generated cases failed, need to fix
            return false;
            // OperationValidatePtr bidirectionalSequenceRnn = std::make_unique<
            //    op_validate::BidirectionalSequenceRnnValidate<HalPlatform::Model,
            //    HalPlatform::Operation>>(model,
            //                                                                          operation);
            // return bidirectionalSequenceRnn->Validate(reason);
        }
        case OperationType::ROI_ALIGN: {
            OperationValidatePtr roiAlign = std::make_unique<
                op_validate::RoiAlignValidate<HalPlatform::Model, HalPlatform::Operation>>(
                model, operation);
            return roiAlign->Validate(reason);
        }
        case OperationType::TOPK_V2: {
            return false;
            // OperationValidatePtr topkV2 = std::make_unique<
            //    op_validate::TopkV2Validate<HalPlatform::Model, HalPlatform::Operation>>(
            //    model, operation);
            // return topkV2->Validate(reason);
        }
        case OperationType::CAST: {
            OperationValidatePtr cast = std::make_unique<
                op_validate::CastValidate<HalPlatform::Model, HalPlatform::Operation>>(model,
                                                                                       operation);
            return cast->Validate(reason);
        }
        case OperationType::QUANTIZE: {
            OperationValidatePtr quantize = std::make_unique<
                op_validate::QuantizeValidate<HalPlatform::Model, HalPlatform::Operation>>(
                model, operation);
            return quantize->Validate(reason);
        }
        case OperationType::SELECT: {
            OperationValidatePtr select = std::make_unique<
                op_validate::SelectValidate<HalPlatform::Model, HalPlatform::Operation>>(model,
                                                                                         operation);
            return select->Validate(reason);
        }
        case OperationType::RANDOM_MULTINOMIAL: {
            OperationValidatePtr random = std::make_unique<
                op_validate::RandomMultinomialValidate<HalPlatform::Model, HalPlatform::Operation>>(
                model, operation);
            return random->Validate(reason);
        }
        case OperationType::HEATMAP_MAX_KEYPOINT: {
            OperationValidatePtr heatmap =
                std::make_unique<op_validate::HeatmapMaxKeypointValidate<HalPlatform::Model,
                                                                         HalPlatform::Operation>>(
                    model, operation);
            return heatmap->Validate(reason);
        }
        case OperationType::CHANNEL_SHUFFLE: {
            OperationValidatePtr channelShuffle = std::make_unique<
                op_validate::ChannelShuffleValidate<HalPlatform::Model, HalPlatform::Operation>>(
                model, operation);
            return channelShuffle->Validate(reason);
        }
        case OperationType::GROUPED_CONV_2D: {
            OperationValidatePtr groupedConv2D = std::make_unique<
                op_validate::GroupedConv2DValidate<HalPlatform::Model, HalPlatform::Operation>>(
                model, operation);
            return groupedConv2D->Validate(reason);
        }
        case OperationType::TRANSPOSE_CONV_2D: {
            OperationValidatePtr transposeConv2D = std::make_unique<
                op_validate::TransposeConv2dValidate<HalPlatform::Model, HalPlatform::Operation>>(
                model, operation);
            return transposeConv2D->Validate(reason);
        }
        case OperationType::RESHAPE: {
            OperationValidatePtr reshape = std::make_unique<
                op_validate::ReshapeValidate<HalPlatform::Model, HalPlatform::Operation>>(
                model, operation);
            return reshape->Validate(reason);
        }
        case OperationType::DEPTHWISE_CONV_2D: {
            OperationValidatePtr depthwiseConv2d = std::make_unique<
                op_validate::DepthwiseConv2dValidate<HalPlatform::Model, HalPlatform::Operation>>(
                model, operation);
            return depthwiseConv2d->Validate(reason);
        }
        case OperationType::SLICE: {
            OperationValidatePtr slice = std::make_unique<
                op_validate::SliceValidate<HalPlatform::Model, HalPlatform::Operation>>(model,
                                                                                        operation);
            return slice->Validate(reason);
        }
        case OperationType::STRIDED_SLICE: {
            auto strided_slice = std::make_unique<
                op_validate::StridedSliceValidate<HalPlatform::Model, HalPlatform::Operation>>(
                model, operation);
            return strided_slice->Validate(reason);
        }
        case OperationType::SQUEEZE: {
            auto squeeze = std::make_unique<
                op_validate::SqueezeValidate<HalPlatform::Model, HalPlatform::Operation>>(
                model, operation);
            return squeeze->Validate(reason);
        }
        case OperationType::MEAN: {
            auto mean = std::make_unique<
                op_validate::MeanValidate<HalPlatform::Model, HalPlatform::Operation>>(model,
                                                                                       operation);
            return mean->Validate(reason);
        }
        case OperationType::BOX_WITH_NMS_LIMIT:
        case OperationType::PAD_V2:
        case OperationType::QUANTIZED_16BIT_LSTM:
        case OperationType::ROI_POOLING:
        case OperationType::TILE:
            isSupport &= false;
            break;
        // NNAPI 1.3 new op
#if ANDROID_SDK_VERSION >= 30
        case OperationType::HARD_SWISH: {
            auto hard_swish =  std::make_unique<
                op_validate::ActivationValidate<HalPlatform::Model, HalPlatform::Operation>>(
                    model, operation);
            return hard_swish->Validate(reason);
        }
        case OperationType::ELU: {
            auto elu =  std::make_unique<
                op_validate::EluValidate<HalPlatform::Model, HalPlatform::Operation>>(
                    model, operation);
            return elu->Validate(reason);
        }
        case OperationType::IF:
        case OperationType::WHILE:
        case OperationType::FILL:
        case OperationType::RANK:
        case OperationType::RESIZE_NEAREST_NEIGHBOR:
            isSupport &= false;
            break;
# endif
#endif // endif ANDOIR_SDK_VERSION >= 29
#if ANDROID_SDK_VERSION >= 28
        default:
            isSupport &= true;
#endif
    }
#if ANDROID_SDK_VERSION >= 29

    // Overall check
    // TODO: if all of LSTM's inputs are constant, the result will fail.
    std::vector<OperationType> whiteList = {
        OperationType::CONCATENATION, OperationType::LSTM, OperationType::MUL};

    // do not support constant tensor as operation's Input except whiteList.
    if (std::find(whiteList.begin(), whiteList.end(), operation.type) == whiteList.end()) {
        if (isConstantTensor(GetHalOperand(model, operation.inputs[0]))) {
            reason += "reject op because its input[0] is constant tensor\n";
            isSupport &= false;
        }
    }

    // TODO: nnapi 1.2 new operand type
    for (size_t i = 0; i < operation.inputs.size(); i++) {
        auto& operand = GetHalOperand(model, operation.inputs[i]);
        if (false == checkSupportedOperand(operand)) {
            reason += "reject op because its operand data type is not supported yet\n";
            isSupport &= false;
        }
    }

    for (size_t i = 0; i < operation.outputs.size(); i++) {
        auto& operand = GetHalOperand(model, operation.inputs[i]);
        if (false == checkSupportedOperand(operand)) {
            reason += "reject op because its operand data type is not supported yet\n";
            isSupport &= false;
        }
    }
#endif

#if ANDROID_SDK_VERSION >= 28
    // Not support dynamic shape
    // Check inputs
    if (0 == operation.inputs.size()) {
        reason += "reject op because no input\n";
        isSupport &= false;
    }
    for (auto inIdx : operation.inputs) {
        auto& dims = GetHalOperand(model, inIdx).dimensions;
        if (isTensor(GetHalOperand(model, inIdx)) && dims.size() == 0) {
            isSupport &= false;
            reason += "reject op because its input tensor rank = 0\n";
        }
        if (dims.size() > 6) {
            isSupport &= false;
            reason += "reject op because its input rank > 6\n";
        }
        for (auto dim : dims) {
            if (dim == 0) {
                reason +=
                    "reject op because its input shape not determined which require shape "
                    "inference\n";
                isSupport &= false;
            }
        }
    }

    if (0 == operation.outputs.size()) {
        reason += "reject op because no output\n";
        isSupport = false;
    }
    for (size_t i = 0; i < operation.outputs.size(); i++) {
        auto& dims = GetHalOperand(model, operation.outputs[i]).dimensions;
        if (isTensor(GetHalOperand(model, operation.outputs[i])) && dims.size() == 0) {
            isSupport &= false;
            reason += "reject op because its output tensor rank = 0\n";
        }
        if (dims.size() > 6) {
            isSupport &= false;
            reason += "reject op because its output rank > 6\n";
        }
        for (auto dimIndex : dims)
            if (dimIndex == 0) {
                reason +=
                    "reject op because its output shape not determined which require shape "
                    "inference\n";
                isSupport &= false;
            }
    }
    return isSupport;
#endif
    return true;
}

bool VsiDriver::isWeightMd5Matched(const HalPlatform::Operation& operation,
                                   const HalPlatform::Model& model,
                                   int block_level) {
    std::vector<std::string> block_list;
    std::copy(model_size::D.begin(), model_size::D.end(), std::back_inserter(block_list));
    if (block_level >= 1) {
        std::copy(model_size::XXL.begin(), model_size::XXL.end(), std::back_inserter(block_list));
    }

    if (block_level >= 2) {
        std::copy(model_size::XL.begin(), model_size::XL.end(), std::back_inserter(block_list));
    }

    if (block_level >= 3) {
        std::copy(model_size::L.begin(), model_size::L.end(), std::back_inserter(block_list));
    }

    if (block_level >= 4) {
        std::copy(model_size::M.begin(), model_size::M.end(), std::back_inserter(block_list));
    }

    if (block_level >= 5) {
        std::copy(model_size::S.begin(), model_size::S.end(), std::back_inserter(block_list));
    }

    if (block_level == 6) {
        std::copy(model_size::XS.begin(), model_size::XS.end(), std::back_inserter(block_list));
    }

    if (!block_list.empty() &&
    (OperationType::CONV_2D == operation.type ||
     OperationType::FULLY_CONNECTED == operation.type)) {
        auto& weight = GetHalOperand(model, operation.inputs[1]);
        if (!isConstantTensor(weight)) return false;
        auto& shape = weight.dimensions;
        // vgg_quant vgg_float srgan_quant srgan_float dped_float crnn_float dped_float/quant
        decltype(weight.dimensions) match_shape_0 = {64, 3, 3, 64};
        // icnet_float inception_v3_float/quant inception_face_float/quant mobilenet_v2_float/quant
        // deeplab_v3_plus_float/quant inceptin_v4
        decltype(weight.dimensions) match_shape_1 = {32, 3, 3, 3};
        // srcnn
        decltype(weight.dimensions) match_shape_2 = {64, 9, 9, 3};
        // unet
        decltype(weight.dimensions) match_shape_3 = {16, 3, 3, 16};
        // mobilenet v1 0.25 128
        decltype(weight.dimensions) match_shape_4 = {1001, 1, 1, 256};
        // mobilenet v1 0.5 160
        decltype(weight.dimensions) match_shape_5 = {1001, 1, 1, 512};
        // mobilenet v1_0.75_192
        decltype(weight.dimensions) match_shape_6 = {1001, 1, 1, 768};
        // mobilenet_v1_1.0_224 inception_v1_quant inception_v2_quant
        decltype(weight.dimensions) match_shape_7 = {1001, 1, 1, 1024};
        // mobilenet_v2_0.75_192
        decltype(weight.dimensions) match_shape_8 = {1001, 1, 1, 1280};
        // ssd_mobilenet_v2_fpnlite_320x320_coco17_quant
        decltype(weight.dimensions) match_shape_9 = {96, 1, 1, 16};
        // lstm_float
        decltype(weight.dimensions) match_shape_10 = {2048, 1012};

        std::list<decltype(weight.dimensions)>
            shape_filter;  // Match shape first to save cost on md5 caculation
        shape_filter.push_back(match_shape_0);
        shape_filter.push_back(match_shape_1);
        shape_filter.push_back(match_shape_2);
        shape_filter.push_back(match_shape_3);
        shape_filter.push_back(match_shape_4);
        shape_filter.push_back(match_shape_5);
        shape_filter.push_back(match_shape_6);
        shape_filter.push_back(match_shape_7);
        shape_filter.push_back(match_shape_8);
        shape_filter.push_back(match_shape_9);
        shape_filter.push_back(match_shape_10);

        bool shape_matched = std::any_of(
            shape_filter.begin(),
            shape_filter.end(),
            [&shape](const decltype(weight.dimensions)& elem) { return shape == elem; });
        if (shape_matched) {
            struct VsiRTInfo rt;
            const char* weight_data =
                reinterpret_cast<const char*>(getOperandDataPtr(model, weight, rt));
            std::string md5_src(weight_data, 512);
            std::string md5 = commonMd5Secret32(md5_src);
            if (std::find(block_list.begin(), block_list.end(), md5) != block_list.end()) {
                LOG(INFO) << "MD5 matched.";
                return true;
            } else {  // keep it for further debug
                // LOG(INFO) << "Debug"<< shape[0] <<"," << shape[1] << "," << shape[2] <<","<<
                // shape[3] << " , md5 = " << md5;
            }
        }
        return false;
    }
    return false;
}

int getSystemPropertyAsInt(const char* prop_name, int default_value) {
    char value[100] = {0};
    if (getSystemProperty(prop_name, value)) {
        return atoi(value);
    } else {
        return default_value;
    }
}

int getSystemProperty(const char* prop_name, char* value) {
    static const char* kPrefixForAndroidR = "vendor.";
    std::string real_prop_name(prop_name);

#if ANDROID_SDK_VERSION >= 30
    real_prop_name = kPrefixForAndroidR + real_prop_name;
#endif

    return __system_property_get(real_prop_name.c_str(), value);
}

}  // namespace vsi_driver
}  // namespace nn
}  // namespace android

using android::sp;
using android::nn::vsi_driver::VsiDriver;

int main() {
    sp<VsiDriver> driver(new VsiDriver());
    return driver->run();
}
