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


#ifndef __ANEURALNETWORKS_REDUCTION_HPP__
#define __ANEURALNETWORKS_REDUCTION_HPP__

#define OP_SPEC_NAME ReduceAll
OP_SPEC_BEGIN()
#define ARG_NAMES         \
    (input,               \
     axes,                \
     keep_dim)
#define ARGC BOOST_PP_TUPLE_SIZE(ARG_NAMES)
#define BOOST_PP_LOCAL_MACRO(n) OP_SPEC_ARG(BOOST_PP_TUPLE_ELEM(ARGC, n, ARG_NAMES))
#define BOOST_PP_LOCAL_LIMITS (0, ARGC)
#include BOOST_PP_LOCAL_ITERATE()
OP_SPEC_END()
// order of argument is important
MAKE_SPEC(base)
    .input_(nnrt::OperandType::TENSOR_BOOL8)
    .axes_(nnrt::OperandType::TENSOR_INT32)
    .keep_dim_(nnrt::OperandType::BOOL));
#undef ARG_NAMES
#undef ARGC
#undef OP_SPEC_NAME

#define OP_SPEC_NAME ReduceAny
OP_SPEC_BEGIN()
#define ARG_NAMES         \
    (input,               \
     axes,                \
     keep_dim)
#define ARGC BOOST_PP_TUPLE_SIZE(ARG_NAMES)
#define BOOST_PP_LOCAL_MACRO(n) OP_SPEC_ARG(BOOST_PP_TUPLE_ELEM(ARGC, n, ARG_NAMES))
#define BOOST_PP_LOCAL_LIMITS (0, ARGC)
#include BOOST_PP_LOCAL_ITERATE()
OP_SPEC_END()
// order of argument is important
MAKE_SPEC(base)
    .input_(nnrt::OperandType::TENSOR_BOOL8)
    .axes_(nnrt::OperandType::TENSOR_INT32)
    .keep_dim_(nnrt::OperandType::BOOL));
#undef ARG_NAMES
#undef ARGC
#undef OP_SPEC_NAME

/**
 * Reduces a tensor by computing the maximum of elements along given
 * dimensions.
 *
 * If keep_dims is true, the reduced dimensions are
 * retained with length 1. Otherwise, the rank of the tensor is reduced by
 * 1 for each entry in dimensions.
 *
 * Supported tensor {@link OperandCode}:
 * * {@link ANEURALNETWORKS_TENSOR_FLOAT16}
 * * {@link ANEURALNETWORKS_TENSOR_FLOAT32}
 * * {@link ANEURALNETWORKS_TENSOR_QUANT8_ASYMM}
 *
 * Supported tensor rank: up to 4
 *
 * Inputs:
 * * 0: An n-D tensor.
 * * 1: A 1-D tensor of {@link ANEURALNETWORKS_TENSOR_INT32}. The dimensions
 *      to reduce. Dimension values must be in the range [-n, n).
 * * 2: An {@link ANEURALNETWORKS_BOOL} scalar, keep_dims. If true,
 *      retains reduced dimensions with length 1.
 *
 * Outputs:
 * * 0: A tensor of the same {@link OperandCode} as input0.
 *
 * Available since API level 29.
 */
#define OP_SPEC_NAME ReduceMax
OP_SPEC_BEGIN()
#define ARG_NAMES         \
    (input,               \
     axes,                \
     keep_dim)
#define ARGC BOOST_PP_TUPLE_SIZE(ARG_NAMES)

#define BOOST_PP_LOCAL_MACRO(n) OP_SPEC_ARG(BOOST_PP_TUPLE_ELEM(ARGC, n, ARG_NAMES))
#define BOOST_PP_LOCAL_LIMITS (0, ARGC)
#include BOOST_PP_LOCAL_ITERATE()
OP_SPEC_END()

// order of argument is important
MAKE_SPEC(float16_base)
    .input_(nnrt::OperandType::TENSOR_FLOAT16)
    .axes_(nnrt::OperandType::TENSOR_INT32)
    .keep_dim_(nnrt::OperandType::BOOL));

    OVERRIDE_SPEC(float16_base, float32)
    .input_(nnrt::OperandType::TENSOR_FLOAT32)
    );

    OVERRIDE_SPEC(float16_base, asymm_u8)
    .input_(nnrt::OperandType::TENSOR_QUANT8_ASYMM)
    );

    OVERRIDE_SPEC(float16_base, asymm_int8)
    .input_(nnrt::OperandType::TENSOR_QUANT8_ASYMM_SIGNED)
    );

#undef ARG_NAMES
#undef ARGC
#undef OP_SPEC_NAME

/**
 * Reduces a tensor by computing the minimum of elements along given
 * dimensions.
 *
 * If keep_dims is true, the reduced dimensions are
 * retained with length 1. Otherwise, the rank of the tensor is reduced by
 * 1 for each entry in dimensions.
 *
 * Supported tensor {@link OperandCode}:
 * * {@link ANEURALNETWORKS_TENSOR_FLOAT16}
 * * {@link ANEURALNETWORKS_TENSOR_FLOAT32}
 * * {@link ANEURALNETWORKS_TENSOR_QUANT8_ASYMM}
 *
 * Supported tensor rank: up to 4
 *
 * Inputs:
 * * 0: An n-D tensor.
 * * 1: A 1-D tensor of {@link ANEURALNETWORKS_TENSOR_INT32}. The dimensions
 *      to reduce. Dimension values must be in the range [-n, n).
 * * 2: An {@link ANEURALNETWORKS_BOOL} scalar, keep_dims. If true,
 *      retains reduced dimensions with length 1.
 *
 * Outputs:
 * * 0: A tensor of the same {@link OperandCode} as input0.
 *
 * Available since API level 29.
 */
#define OP_SPEC_NAME ReduceMin
OP_SPEC_BEGIN()
#define ARG_NAMES         \
    (input,               \
     axes,                \
     keep_dim)
#define ARGC BOOST_PP_TUPLE_SIZE(ARG_NAMES)

#define BOOST_PP_LOCAL_MACRO(n) OP_SPEC_ARG(BOOST_PP_TUPLE_ELEM(ARGC, n, ARG_NAMES))
#define BOOST_PP_LOCAL_LIMITS (0, ARGC)
#include BOOST_PP_LOCAL_ITERATE()
OP_SPEC_END()

// order of argument is important
MAKE_SPEC(float16_base)
    .input_(nnrt::OperandType::TENSOR_FLOAT16)
    .axes_(nnrt::OperandType::TENSOR_INT32)
    .keep_dim_(nnrt::OperandType::BOOL));

    OVERRIDE_SPEC(float16_base, float32)
    .input_(nnrt::OperandType::TENSOR_FLOAT32)
    );

    OVERRIDE_SPEC(float16_base, quant8_asymm)
    .input_(nnrt::OperandType::TENSOR_QUANT8_ASYMM)
    );

    OVERRIDE_SPEC(float16_base, asymm_int8)
    .input_(nnrt::OperandType::TENSOR_QUANT8_ASYMM_SIGNED)
    );

#undef ARG_NAMES
#undef ARGC
#undef OP_SPEC_NAME

/**
 * Reduces a tensor by summing elements along given dimensions.
 *
 * If keep_dims is true, the reduced dimensions are
 * retained with length 1. Otherwise, the rank of the tensor is reduced by
 * 1 for each entry in dimensions.
 *
 * Supported tensor {@link OperandCode}:
 * * {@link ANEURALNETWORKS_TENSOR_FLOAT16}
 * * {@link ANEURALNETWORKS_TENSOR_FLOAT32}
 *
 * Supported tensor rank: up to 4
 *
 * Inputs:
 * * 0: An n-D tensor.
 * * 1: A 1-D tensor of {@link ANEURALNETWORKS_TENSOR_INT32}. The dimensions
 *      to reduce. Dimension values must be in the range [-n, n).
 * * 2: An {@link ANEURALNETWORKS_BOOL} scalar, keep_dims. If true,
 *      retains reduced dimensions with length 1.
 *
 * Outputs:
 * * 0: A tensor of the same {@link OperandCode} as input0.
 *
 * Available since API level 29.
 */
#define OP_SPEC_NAME ReduceSum
OP_SPEC_BEGIN()
#define ARG_NAMES         \
    (input,               \
     axes,                \
     keep_dim)
#define ARGC BOOST_PP_TUPLE_SIZE(ARG_NAMES)

#define BOOST_PP_LOCAL_MACRO(n) OP_SPEC_ARG(BOOST_PP_TUPLE_ELEM(ARGC, n, ARG_NAMES))
#define BOOST_PP_LOCAL_LIMITS (0, ARGC)
#include BOOST_PP_LOCAL_ITERATE()
OP_SPEC_END()

// order of argument is important
MAKE_SPEC(float16_base)
    .input_(nnrt::OperandType::TENSOR_FLOAT16)
    .axes_(nnrt::OperandType::TENSOR_INT32)
    .keep_dim_(nnrt::OperandType::BOOL));

    OVERRIDE_SPEC(float16_base, float32)
    .input_(nnrt::OperandType::TENSOR_FLOAT32)
    );

#undef ARG_NAMES
#undef ARGC
#undef OP_SPEC_NAME

/**
 * Reduces a tensor by multiplying elements along given dimensions.
 *
 * If keep_dims is true, the reduced dimensions are
 * retained with length 1. Otherwise, the rank of the tensor is reduced by
 * 1 for each entry in dimensions.
 *
 * Supported tensor {@link OperandCode}:
 * * {@link ANEURALNETWORKS_TENSOR_FLOAT16}
 * * {@link ANEURALNETWORKS_TENSOR_FLOAT32}
 *
 * Supported tensor rank: up to 4
 *
 * Inputs:
 * * 0: An n-D tensor.
 * * 1: A 1-D tensor of {@link ANEURALNETWORKS_TENSOR_INT32}. The dimensions
 *      to reduce. Dimension values must be in the range [-n, n).
 * * 2: An {@link ANEURALNETWORKS_BOOL} scalar, keep_dims. If true,
 *      retains reduced dimensions with length 1.
 *
 * Outputs:
 * * 0: A tensor of the same {@link OperandCode} as input0.
 *
 * Available since API level 29.
 */
#define OP_SPEC_NAME ReduceProd
OP_SPEC_BEGIN()
#define ARG_NAMES         \
    (input,               \
     axes,                \
     keep_dim)
#define ARGC BOOST_PP_TUPLE_SIZE(ARG_NAMES)

#define BOOST_PP_LOCAL_MACRO(n) OP_SPEC_ARG(BOOST_PP_TUPLE_ELEM(ARGC, n, ARG_NAMES))
#define BOOST_PP_LOCAL_LIMITS (0, ARGC)
#include BOOST_PP_LOCAL_ITERATE()
OP_SPEC_END()

// order of argument is important
MAKE_SPEC(float16_base)
    .input_(nnrt::OperandType::TENSOR_FLOAT16)
    .axes_(nnrt::OperandType::TENSOR_INT32)
    .keep_dim_(nnrt::OperandType::BOOL));

    OVERRIDE_SPEC(float16_base, float32)
    .input_(nnrt::OperandType::TENSOR_FLOAT32)
    );

#undef ARG_NAMES
#undef ARGC
#undef OP_SPEC_NAME

#endif
