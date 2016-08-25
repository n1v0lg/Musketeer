// Copyright (c) 2016 Nikolaj Volgushev <nikolaj@bu.edu>

/*
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * THIS CODE IS PROVIDED ON AN *AS IS* BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT
 * LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR
 * A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.
 *
 * See the Apache Version 2.0 License for specific language governing
 * permissions and limitations under the License.
 */

#ifndef MUSKETEER_OBLIGATION_H
#define MUSKETEER_OBLIGATION_H

#include "base/utils.h"
#include "base/common.h"
#include "ir/aggregation.h"
#include "ir/relation.h"
#include "ir/select_operator.h"
#include "ir/mul_operator.h"
#include "frontends/operator_node.h"

#include <boost/lexical_cast.hpp>
#include <map>

using namespace musketeer::ir;

namespace musketeer {
namespace mpc {

    // TODO(nikolaj): Make an obligation class hiearchy
    class Obligation {
    public:
        Obligation(shared_ptr<OperatorNode> op_node, int index);
        ~Obligation() {
            // TODO(nikolaj): Implement.
        }

        OperatorInterface* get_operator();
        
        // Might need to add methods for taking into account another obligation
        // when dealing with binary op_nodes, i.e., unions etc.
        /*
            Must call this method when pushing the obligation through a node.
            Relations, columns, etc. on op will get updated.
        */
        void PassThrough(shared_ptr<OperatorNode> op_node);
        bool CanPassOperator(OperatorInterface* other);

        bool CanPass(SelectOperator* other);
        bool CanPass(MulOperator* other);

        friend std::ostream& operator<<(std::ostream& _stream, Obligation const& obl) { 
            // TODO(nikolaj): Implement.
            _stream << "Obligation(" << ")";
            return _stream;
        };
        
    private:
        Aggregation* op;
        GroupByType type;

        void update_op_columns(OperatorInterface* parent);
    }; 

} // namespace mpc
} // namespace musketeer

#endif
