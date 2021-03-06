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

#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/time.h>

#include "graphchi_basic_includes.hpp"

using namespace graphchi;
using namespace std;

typedef {{VERTEX_DATA_TYPE}} VertexDataType;
typedef {{EDGE_DATA_TYPE}} EdgeDataType;

struct {{CLASS_NAME}} : public GraphChiProgram<VertexDataType, EdgeDataType> {
  void before_iteration(int iteration, graphchi_context &ginfo) {
  }

  void after_iteration(int iteration, graphchi_context &ginfo) {
  }

  void before_exec_interval(vid_t window_st, vid_t window_en,
                            graphchi_context &ginfo) {
  }

  void update(graphchi_vertex<VertexDataType, EdgeDataType> &v,
              graphchi_context &ginfo) {
    EdgeDataType n_out_edges = v.num_outedges();
    if (ginfo.iteration == 0) {
      EdgeDataType in_edge_val = v.get_data();
      {{PRE_GROUP_VERTEX_VAL}}
      // On the first iteration add the values to the edges.
      for (int i = 0; i < v.num_outedges(); i++) {
        v.outedge(i)->set_data(in_edge_val);
      }
    } else {
      VertexDataType ver_val = {{INIT_VER_VAL}};
      EdgeDataType in_edge_val = 0;
      for (int i = 0; i < v.num_inedges(); i++) {
        in_edge_val = v.inedge(i)->get_data();
        {{GROUP_VERTEX_VAL}}
      }
      {{POST_GROUP_VERTEX_VAL}}
      in_edge_val = ver_val;
      {{PRE_GROUP_VERTEX_VAL}}
      for (int i = 0; i < v.num_outedges(); i++) {
        v.outedge(i)->set_data(in_edge_val);
      }
      v.set_data(ver_val);
    }
  }
};

class OutputVertexCallback : public VCallback<VertexDataType> {
 public:
  OutputVertexCallback(): VCallback<VertexDataType>(),
                          outfile("/tmp{{EDGES_PATH}}output") {
  }

  ~OutputVertexCallback() {
    outfile.close();
  }

  ofstream outfile;

  virtual void callback(vid_t vertex_id, VertexDataType &value) {
    outfile << vertex_id << " " << value << endl;
  }
};

int main(int argc, const char** argv) {
  graphchi_init(argc, argv);
  metrics met("{{CLASS_NAME}}");
  int niters = get_option_int("niters", {{N_ITERS}});
  string filetype = get_option_string("filetype", "edgelist");
  bool scheduler = false;
  timeval start_proc;
  gettimeofday(&start_proc, NULL);
  int num_shards = convert_if_notexists<{{EDGE_DATA_TYPE}}>(
      "/tmp{{EDGES_PATH}}input", get_option_string("nshards", "auto"));
  graphchi_engine<VertexDataType, EdgeDataType> engine(
      "/tmp{{EDGES_PATH}}input", num_shards, scheduler, met);
  timeval end_proc;
  gettimeofday(&end_proc, NULL);
  engine.set_modifies_inedges(false);
  {{CLASS_NAME}} program;
  engine.run(program, niters);
  timeval end_run;
  gettimeofday(&end_run, NULL);
  long loading_data = end_proc.tv_sec - start_proc.tv_sec;
  long run_time = end_run.tv_sec - end_proc.tv_sec;
  cout << "LOADING DATA: " << loading_data << endl;
  cout << "RUN TIME: " << run_time << endl;
  //  OutputVertexCallback output_callback;
  //  foreach_vertices<VertexDataType>("/tmp{{EDGES_PATH}}input", 0,
  //                                   engine.num_vertices(), output_callback);
  metrics_report(met);
  return 0;
}
