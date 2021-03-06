// Copyright (c) 2015 Ionel Gog <ionel.gog@cl.cam.ac.uk>

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

#include "translation/translator_viff.h"

#include <boost/lexical_cast.hpp>
#include <ctemplate/template.h>
#include <sys/time.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <queue>
#include <set>
#include <string>

#include "base/common.h"
#include "ir/column.h"
#include "ir/condition_tree.h"

namespace musketeer {
namespace translator {

  using ctemplate::mutable_default_template_cache;

  map<string, Relation*> TranslatorViff::relations;

  TranslatorViff::TranslatorViff(const op_nodes& dag,
                                 const string& class_name):
    TranslatorInterface(dag, class_name) {
  }

  string TranslatorViff::GetBinaryPath(OperatorInterface* op) {
    return op->get_code_dir() + class_name + "_code/" + class_name + ".py";
  }

  string TranslatorViff::GetSourcePath(OperatorInterface* op) {
    return op->get_code_dir() + class_name + "_code/" + class_name + ".py";
  }

  // taken from translator_hadoop presumably along with a bug
  set<pair<Relation*, string>> TranslatorViff::GetInputRelsAndPaths(const op_nodes& dag) {
    queue<shared_ptr<OperatorNode>> to_visit;
    set<shared_ptr<OperatorNode>> visited;
    set<pair<Relation*, string>> input_rels_paths;
    set<string> known_rels;
    for (op_nodes::const_iterator it = dag.begin(); it != dag.end(); ++it) {
      to_visit.push(*it);
      visited.insert(*it);
    }
    while (!to_visit.empty()) {
      shared_ptr<OperatorNode> cur_node = to_visit.front();
      to_visit.pop();
      OperatorInterface* op = cur_node->get_operator();
      vector<Relation*> relations = op->get_relations();
      string output_relation = op->get_output_relation()->get_name();
      for (vector<Relation*>::iterator rel_it = relations.begin();
           rel_it != relations.end(); ++rel_it) {
        string rel_name = (*rel_it)->get_name();
        if (known_rels.insert(rel_name).second) {
          input_rels_paths.insert(make_pair(*rel_it, op->CreateInputPath(*rel_it)));
        }
      }
      known_rels.insert(output_relation);
      if (!cur_node->IsLeaf()) {
        op_nodes children = cur_node->get_loop_children();
        op_nodes non_loop_children = cur_node->get_children();
        children.insert(children.end(), non_loop_children.begin(),
                        non_loop_children.end());
        for (op_nodes::iterator it = children.begin(); it != children.end();
             ++it) {
          if (visited.insert(*it).second) {
            to_visit.push(*it);
          }
        }
      }
    }
    return input_rels_paths;
  }

  vector<Relation*>* TranslatorViff::DetermineInputsSpark(const op_nodes& dag,
                                                           set<string>* inputs,
                                                           set<string>* visited) {
    vector<Relation*> *input_rels_out = new vector<Relation*>;
    queue<shared_ptr<OperatorNode> > to_visit;
    for (op_nodes::const_iterator it = dag.begin(); it != dag.end(); ++it) {
      to_visit.push(*it);
      Relation* rel = (*it)->get_operator()->get_output_relation();
      visited->insert(rel->get_name());
      relations[rel->get_name()] = rel;
    }
    while (!to_visit.empty()) {
      shared_ptr<OperatorNode> node = to_visit.front();
      to_visit.pop();
      OperatorInterface* op = node->get_operator();
      LOG(INFO) << "VISITED: " << op->get_output_relation()->get_name();
      vector<Relation*> input_rels = op->get_relations();
      for (vector<Relation*>::iterator it = input_rels.begin();
           it != input_rels.end(); ++it) {
        // Rel is an input if rel not visited or doesn't have parents and
        // not already marked as an input.
        if ((node->get_parents().size() == 0 ||
             !IsGeneratedByOp((*it)->get_name(), node->get_parents())) &&
            visited->find((*it)->get_name()) == visited->end() &&
            inputs->find((*it)->get_name()) == inputs->end()) {
          LOG(INFO) << (*it)->get_name() << " is an input";
          inputs->insert((*it)->get_name());
          input_rels_out->push_back(*it);
          relations[(*it)->get_name()]= *it;
        } else {
          relations[(*it)->get_name()] = *it;
          LOG(INFO) << (*it)->get_name() << " is not an input";
        }
      }
      op_nodes non_loop_children = node->get_children();
      op_nodes children = node->get_loop_children();
      children.insert(children.end(), non_loop_children.begin(),
                      non_loop_children.end());
      for (op_nodes::iterator it = children.begin(); it != children.end();
           ++it) {
        bool can_add = true;
        vector<Relation*> input_rels = (*it)->get_operator()->get_relations();
        for (vector<Relation*>::iterator input_it = input_rels.begin();
             input_it != input_rels.end(); ++input_it) {
          if (visited->find((*input_it)->get_name()) == visited->end() &&
              inputs->find((*input_it)->get_name()) == inputs->end() &&
              IsGeneratedByOp((*input_it)->get_name(), node->get_parents())) {
            can_add = false;
            break;
          }
        }
        if (can_add || op->get_type() == WHILE_OP ||
            (*it)->get_operator()->get_type() == WHILE_OP) {
          Relation* rel = (*it)->get_operator()->get_output_relation();
          relations[rel->get_name()] = rel;
          if (visited->insert(rel->get_name()).second) {
            to_visit.push(*it);
          }
        }
      }
    }
    return input_rels_out;
  }

  // Check if all the inputs of the operator have been processed.
  bool TranslatorViff::CanSchedule(OperatorInterface* op,
                                   set<string>* processed) {
    string output = op->get_output_relation()->get_name();
    if (processed->find(output) != processed->end()) {
      LOG(INFO) << "Operator already scheduled";
      return false;
    }
    vector<Relation*> inputs = op->get_relations();
    for (vector<Relation*>::iterator it = inputs.begin(); it != inputs.end();
         ++it) {
      if (processed->find((*it)->get_name()) == processed->end()) {
        LOG(INFO) << "Cannot schedule yet: " << (*it)->get_name()
                  << " is missing";
        return false;
      }
    }
    LOG(INFO) << "Can Schedule ";
    return true;
  }

  void TranslatorViff::TranslateDAG(string* code, const op_nodes& next_set,
                                    set<shared_ptr<OperatorNode>>* leaves,
                                    set<string>* processed) {
    for (op_nodes::const_iterator it = next_set.begin(); it != next_set.end(); ++it) {
      shared_ptr<OperatorNode> node = *it;
      OperatorInterface* op = (*it)->get_operator();
      string output_rel = op->get_output_relation()->get_name();
      LOG(INFO) << "Translating for " << output_rel;
      if (CanSchedule(op, processed)) {
        ViffJobCode* job_code = dynamic_cast<ViffJobCode*>(TranslateOperator(op));
        *code += job_code->get_code();
        processed->insert(output_rel);
        if (node->IsLeaf()) {
          leaves->insert(node);
        }
        else {
          TranslateDAG(code, node->get_children(), leaves, processed);
        }
      }
      else {
        LOG(INFO) << "Cannot schedule operator yet: " 
                  << op->get_output_relation()->get_name();
      }
    }
  }

  string TranslatorViff::TranslateImportAndUtils() {
    string import_and_utils;
    TemplateDictionary dict("import_and_utils");
    ExpandTemplate(FLAGS_viff_templates_dir + "ImportAndUtilsTemplate.py",
                   ctemplate::DO_NOT_STRIP, &dict, &import_and_utils);
    return import_and_utils;
  }

  string TranslatorViff::TranslateCloseProtocol() {
    string close_protocol;
    TemplateDictionary dict("close_protocol");
    ExpandTemplate(FLAGS_viff_templates_dir + "CloseProtocolTemplate.py",
                   ctemplate::DO_NOT_STRIP, &dict, &close_protocol);
    return close_protocol;
  }

  string TranslatorViff::TranslateMain() {
    string main;
    TemplateDictionary dict("main");
    dict.SetValue("VIFF_CONFIG_LOC", FLAGS_viff_config_loc);
    ExpandTemplate(FLAGS_viff_templates_dir + "MainTemplate.py",
                   ctemplate::DO_NOT_STRIP, &dict, &main);
    return main;
  }

  string TranslatorViff::TranslateInput(set<pair<Relation*, string>> input_rels_paths) {
    string input_rels_code = "";
    for (set<pair<Relation*, string>>::iterator it = input_rels_paths.begin(); 
         it != input_rels_paths.end(); ++it) {
      string in_code;
      LOG(INFO) << "My MPC ID: " << FLAGS_data_owner_id;
      LOG(INFO) << "Owner string of input relation " << it->first->get_name() << ": " << it->first->get_owner_string(); 
      string input_flag = it->first->has_owner(FLAGS_data_owner_id) ? "True" : "False";
      TemplateDictionary dict("input");
      dict.SetValue("REL", it->first->get_name());
      dict.SetValue("INPUT_PATH", it->second);
      dict.SetValue("INPUT_FLAG", input_flag);
      ExpandTemplate(FLAGS_viff_templates_dir + "InputTemplate.py",
                     ctemplate::DO_NOT_STRIP, &dict, &in_code);
      input_rels_code += in_code;
    }
    return input_rels_code;
  }

  string TranslatorViff::TranslateOutput(set<shared_ptr<OperatorNode>> leaves) {
    string store_code = "";
    for (set<shared_ptr<OperatorNode>>::iterator i = leaves.begin(); i != leaves.end(); ++i) {
      TemplateDictionary dict("store");
      dict.SetValue("REL", (*i)->get_operator()->get_output_relation()->get_name());
      dict.SetValue("OUTPUT_PATH", (*i)->get_operator()->get_output_path());
      string cur_code;
      ExpandTemplate(FLAGS_viff_templates_dir + "OutputTemplate.py",
                     ctemplate::DO_NOT_STRIP, &dict, &cur_code);
      store_code += cur_code;
    }
    return store_code;
  }

  string TranslatorViff::GenerateCode() {
    LOG(INFO) << "Viff generate code";
    shared_ptr<OperatorNode> op_node = dag[0];
    OperatorInterface* op = op_node->get_operator();
    std::vector<Relation*> v = op->get_relations();
    
    string import_and_utils = TranslateImportAndUtils();
    set<pair<Relation*, string>> input_rels_paths = GetInputRelsAndPaths(dag);
    string inputs = TranslateInput(input_rels_paths);
    
    string protocol_ops;
    set<shared_ptr<OperatorNode>> leaves = set<shared_ptr<OperatorNode>>();
    set<string> proc;
    
    for (set<pair<Relation*, string>>::iterator i = input_rels_paths.begin(); 
         i != input_rels_paths.end(); ++i) {
      proc.insert(i->first->get_name());
    }

    TranslateDAG(&protocol_ops, dag, &leaves, &proc);
    string output = TranslateOutput(leaves);
    string close_protocol = TranslateCloseProtocol();
    string main = TranslateMain();
    string code = import_and_utils + inputs + protocol_ops + output
                  + close_protocol + main;

    return WriteToFiles(op, code);
  }

  ViffJobCode* TranslatorViff::Translate(SelectOperatorMPC* op) {
    // TODO(nikolaj): Implement non-dummy version
    TemplateDictionary dict("select");
    Relation* input_rel = op->get_relations()[0];
    string input_name = input_rel->get_name();
    dict.SetValue("OUT_REL", op->get_output_relation()->get_name());
    dict.SetValue("IN_REL", input_name);
    string code;
    ExpandTemplate(FLAGS_viff_templates_dir + "SelectTemplate.py",
                   ctemplate::DO_NOT_STRIP, &dict, &code);
    ViffJobCode* job_code = new ViffJobCode(op, code);
    return job_code;
  }

  ViffJobCode* TranslatorViff::Translate(AggOperatorMPC* op) {
    TemplateDictionary dict("aggsec");
    Relation* input_rel = op->get_relations()[0];
    string input_name = input_rel->get_name();
    vector<Column*> group_by_cols = op->get_group_bys();  
    Column* agg_col = op->get_columns()[0];
    string agg_op = GenerateAggMPCOp(op->get_operator());
    dict.SetValue("OUT_REL", op->get_output_relation()->get_name());
    dict.SetValue("IN_REL", input_name);
    dict.SetValue("GROUP_BY_COL", boost::lexical_cast<string>(group_by_cols[0]->get_index()));
    dict.SetValue("AGG_COL", boost::lexical_cast<string>(agg_col->get_index()));
    dict.SetValue("AGG_OP", agg_op);
    string code;
    ExpandTemplate(FLAGS_viff_templates_dir + "AggMPCTemplate.py",
                   ctemplate::DO_NOT_STRIP, &dict, &code);
    ViffJobCode* job_code = new ViffJobCode(op, code);
    return job_code;
  }

  ViffJobCode* TranslatorViff::Translate(JoinOperatorMPC* op) {
    TemplateDictionary dict("joinsec");
    vector<Relation*> relations = op->get_relations();
    dict.SetValue("OUT_REL", op->get_output_relation()->get_name());
    dict.SetValue("LEFT_REL", relations[0]->get_name());
    dict.SetValue("RIGHT_REL", relations[1]->get_name());
    dict.SetValue("LEFT_COL", boost::lexical_cast<string>(op->get_col_left()->get_index()));
    dict.SetValue("RIGHT_COL", boost::lexical_cast<string>(op->get_col_right()->get_index()));
    string code;
    ExpandTemplate(FLAGS_viff_templates_dir + "JoinMPCTemplate.py",
                   ctemplate::DO_NOT_STRIP, &dict, &code);
    ViffJobCode* job_code = new ViffJobCode(op, code);
    return job_code;
  }

  ViffJobCode* TranslatorViff::Translate(MulOperatorMPC* op) {
    return TranslateMathOp(op, op->get_values(), op->get_condition_tree(), "*");
  }

  ViffJobCode* TranslatorViff::Translate(DivOperatorMPC* op) {
    return TranslateMathOp(op, op->get_values(), op->get_condition_tree(), "/");
  }

  string TranslatorViff::GenerateColumnTypes(Relation* rel) {
    vector<Column*> cols = rel->get_columns();
    string types = "(";
    for (vector<Column*>::iterator i = cols.begin(); i != cols.end(); ++i) {
      types += (*i)->translateTypeConversionViff() + ", ";
    }
    return types + ")";
  }

  string TranslatorViff::GenerateAggMPCOp(const string& op) {
    if (op == "+") {
      return "sum";
    }
    else {
      LOG(ERROR) << "Unknown or unsupported agg operator: " << op;
      return "unknown";      
    }
  }

  string TranslatorViff::GenerateColumns(vector<Column*> columns) {
    string col_string = "(";
    for (vector<Column*>::iterator i = columns.begin(); i != columns.end(); ++i) {
      col_string += (*i)->toString("viff") + ", ";
    }
    col_string += ")";
    LOG(INFO) << col_string;
    return col_string;
  }

  ViffJobCode* TranslatorViff::TranslateMathOp(OperatorInterface* op, vector<Value*> values,
                                               ConditionTree* condition_tree, string math_op) {
    Relation* input_rel = op->get_relations()[0];
    string input_name = input_rel->get_name();
    Relation* output_rel = op->get_output_relation();
    string lambda = GenerateLambda(math_op, input_rel,
                                   values[0], values[1], output_rel);
    LOG(INFO) << lambda;
    TemplateDictionary dict("math");
    dict.SetValue("OUT_REL",output_rel->get_name());
    dict.SetValue("IN_REL", input_name);
    dict.SetValue("LAMBDA", lambda);
    string code = "";
    ExpandTemplate(FLAGS_viff_templates_dir + "MathMPCTemplate.py",
                   ctemplate::DO_NOT_STRIP, &dict, &code);
    ViffJobCode* job_code = new ViffJobCode(op, code);
    return job_code;
  }

  string TranslatorViff::GenerateLambda(const string& op,
                                        Relation* rel, Value* left_val,
                                        Value* right_val, Relation* output_rel) {
    vector<Column*> columns = rel->get_columns();
    Column* left_column = dynamic_cast<Column*>(left_val);
    Column* right_column = dynamic_cast<Column*>(right_val);
    string left_value = "";
    string right_value = "";
    int32_t col_index_left = -1;
    int32_t col_index_right = -1;
    string maths = "lambda ";
    for (vector<Column*>::const_iterator it = columns.begin(); it != columns.end(); ++it) {
      int32_t col_index = (*it)->get_index();
      maths += "e" + boost::lexical_cast<string>(col_index + 1);
      if (it != columns.end() - 1) {
        maths += ", ";
      }
    }
    maths += ": [";

    string input_name = "e";

    // Assumption: no two constants
    if (left_column != NULL) {
      col_index_left = left_column->get_index();
    } else {
      left_value = left_val->get_value();
    }
    if (right_column != NULL) {
      col_index_right = right_column->get_index();
    } else {
      right_value = right_val->get_value();
    }
    uint32_t i = 0;
    for (vector<Column*>::const_iterator it = columns.begin(); it != columns.end(); ++it) {
      int32_t col_index = (*it)->get_index();
      if (col_index == col_index_left) {
        if (columns.size() > 1) {
          if (!op.compare("/")) {
            maths += "divide(" + input_name + boost::lexical_cast<string>(col_index + 1) + ", ";
          }
          else {
            maths += input_name + boost::lexical_cast<string>(col_index + 1) +
              " " + op + " ";
          }
        } else {
          maths += input_name + " " + op + " ";
        }
        if (col_index_right != -1) {
          if (columns.size() > 1) {
            if (!op.compare("/")) {
              maths += input_name + boost::lexical_cast<string>(col_index_right + 1) + ")";
            }
            else {
              maths += input_name + boost::lexical_cast<string>(col_index_right + 1);
            }
          } else {
            maths += input_name;
          }
        } else {
          maths += right_value;
        }
      } else if (col_index == col_index_right && (col_index_left == -1)) {
        if (columns.size() > 1) {
          maths += input_name + boost::lexical_cast<string>(col_index + 1) + " " + op +
              " " + left_value;
        } else {
          maths += input_name + " " + op + " " + left_value;
        }
      } else {
        if (columns.size() > 1) {
          maths += input_name + boost::lexical_cast<string>(col_index + 1);
        } else {
          maths += input_name;
        }
      }
      if (++i < columns.size()) {
        maths += ", ";
      }
    }
    maths += "]";
    return maths;
  }

  string TranslatorViff::WriteToFiles(OperatorInterface* op, 
                                      const string& op_code) {
    ofstream job_file;
    string source_file = GetSourcePath(op);
    string path = op->get_code_dir() + class_name + "_code/";
    string create_dir = "mkdir -p " + path;
    std::system(create_dir.c_str());
    job_file.open(source_file.c_str());
    job_file << op_code;
    job_file.close();
    return source_file;
  }

} // namespace translator
} // namespace musketeer
