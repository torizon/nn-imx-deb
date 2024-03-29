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
#ifndef __OVXLIB_PREPARED_MODEL_H__
#define __OVXLIB_PREPARED_MODEL_H__

#include <vector>

#include "nnrt/model.hpp"
#include "nnrt/interpreter.hpp"
#include "nnrt/shared_context.hpp"

extern "C" {
struct _vsi_nn_graph;
typedef struct _vsi_nn_graph vsi_nn_graph_t;
}

namespace nnrt
{
class PreparedModel;

using PreparedModelPtr = std::shared_ptr<PreparedModel>;

struct ExecutionIO;

using ExecutionIOPtr = std::shared_ptr<ExecutionIO>;

class PreparedModel
{
    public:
        PreparedModel(Model* model, SharedContextPtr context,
                const std::vector<ExecutionIOPtr> &inputs,
                Interpreter* interpreter = NULL);
        ~PreparedModel();

        vsi_nn_graph_t* get() {return graph_;}

        int prepare();

        int execute();

        int setInput(uint32_t index, const void* data, size_t length);

        int getOutput(uint32_t index, void* data, size_t length);

        int updateOutputOperand(uint32_t index, const op::OperandPtr operand_type);

        int updateInputOperand(uint32_t index, const op::OperandPtr operand_type);

        void setInterpreter(Interpreter* interpreter) {
            interpreter_ = interpreter;
        }

        std::string signature() { return model_->signature(); }

    private:
        // disable copy constructor
        PreparedModel(const PreparedModel&) = delete;
        PreparedModel(PreparedModel&&) = default;
        PreparedModel& operator=(const PreparedModel&) = delete;

    private:
        vsi_nn_graph_t* graph_{nullptr};
        Model* model_{nullptr};
        Interpreter* interpreter_;

        // Implementation details
        struct Private;
        std::unique_ptr<Private> d;
};
}

#endif
