#ifndef _AUTOSA_TUNING_H
#define _AUTOSA_TUNING_H

#include <isl/schedule.h>
#include <isl/schedule_node.h>
#include <isl/constraint.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include "json.hpp"
#include "autosa_utils.h"

using json = nlohmann::json;

#if defined(__cplusplus)
extern "C" {
#endif    

//class TPTransformHistory {
//    public:
//        TPTransformHistory(){}
//};

//class TPStatement {
//    public:         
//};

class TPExpr {
    public:
        TPExpr() {func = "NULL";}
        TPExpr(std::string f, TPExpr *op) {
            func = f;
            ops.push_back(op);
        }
        TPExpr(std::string f, TPExpr *op1, TPExpr *op2) {
            func = f;
            ops.push_back(op1);
            ops.push_back(op2);
        }

        TPExpr *div_by_param(TPExpr *divisor);
        TPExpr *ceil();
        TPExpr *add(TPExpr *expr);        
        TPExpr *mul(TPExpr *expr);
        TPExpr *subtract(TPExpr *expr); // TODO
        TPExpr *min(TPExpr *expr);
        TPExpr *max(TPExpr *expr);

        TPExpr *infer_bound(
            std::unordered_map<std::string, TPExpr *> lbs, 
            std::unordered_map<std::string, TPExpr *> ubs,
            std::unordered_set<std::string> ignore, int max);
        TPExpr *simplify();
        TPExpr *replace(TPExpr *match, TPExpr *replace);
        TPExpr *dup();
        virtual std::string to_str();
        
        std::string func; // [floor, ceil, div, literal, mul, null, min, max, sub, add]
        std::vector<TPExpr *> ops;        
        
        virtual ~TPExpr() {            
            for (int i = 0; i < ops.size(); i++) {                
                delete ops[i];
            }            
        }
};

class TPIterator {
    public:
        TPIterator(){}
        TPIterator(std::string n, TPExpr *l, TPExpr *u) {
            name = n;
            lb = l;
            ub = u;
        }
        std::shared_ptr<TPExpr> compute_size();
        std::string name;
        TPExpr *lb;
        TPExpr *ub;     
        std::string space_time;
        ~TPIterator() {
            delete lb;
            delete ub;
        }
};

/* Tunable parameters by the tuner. */
class TPParameter: public TPExpr {
    public:
        TPParameter() {}
        TPParameter(std::string n) {
            name = n;
            type = "param";        
            tune = false;
            split_by = NULL;
        }
        TPParameter(std::string n_prefix, int cnt) {
            if (cnt == 0) {
                name = n_prefix;
            } else {
                /* Tiling factors. */
                name = n_prefix + "_t" + std::to_string(cnt);
            }
            name_prefix = n_prefix;
            type = "param";        
            tune = false;
            split_by = NULL;
        }
        TPParameter(TPParameter *p) {
            name = p->name;
            name_prefix = p->name_prefix;
            type = p->type;            
            tune = p->tune;
            attr = p->attr;                        
            split_by = p->split_by;
        }     
        TPParameter *dup();
        std::string to_str();

        std::string name;
        std::string name_prefix;
        std::string type;        
        std::vector<std::shared_ptr<TPExpr>> bounds;        
        bool tune;
        /* The parameter is divisors of the following exps. */
        std::vector <std::shared_ptr<TPExpr>> divisors; 
        /* The parameter is multiples of the following exps. */
        std::vector <std::shared_ptr<TPExpr>> multiples;    
        TPParameter *split_by;
        /* Other constraint tags for this parameters. 
         * "power_of_two", this parameter should be a power of 2.
         * "auto_infer", this parameter will be auto-inferred by other parameters.
         * "external", this parameter will be provided externally.
         */
        std::unordered_set<std::string> tags;
        std::string attr;
        virtual ~TPParameter(){
            //for (int i = 0; i < bounds.size(); i++)
            //    delete bounds[i];
            //for (int i = 0; i > divisors.size(); i++)
            //    delete divisors[i];
            //for (int i = 0; i > multiples.size(); i++)
            //    delete multiples[i];
        }
};

class TPConst: public TPExpr {
    public:
        TPConst() {}
        TPConst(int v) {
            type = "const";
            val = v;
        }
        TPConst *dup();

        std::string type;
        int val;
};

class TPArrayRef {
    public:
        TPArrayRef(){}
        TPArrayRef(std::string n, std::vector<TPExpr *> idx) {
            name = n;
            for (auto i : idx) {
                index.push_back(i);
            }
        }
        std::string name;
        std::vector<TPExpr *> index;
        std::string to_str();
        ~TPArrayRef() {
            for (auto i : index) {
                delete i;
            }
        }
};

class TPArray {
    public:
        TPArray(){}
        TPArray(std::string n) {name = n;}
        std::string name;
        std::vector<std::shared_ptr<TPArrayRef>> refs;
        ~TPArray() {
            //for (auto ref : refs) 
            //    delete ref;
        }
};

class TPArrayTile {
    public:
        TPArrayTile(){data_pack_factor_inter = NULL; data_pack_factor_intra = NULL;}
        std::string name;
        std::string type;
        int ele_size; 
        std::vector<TPExpr *> lbs;
        std::vector<TPExpr *> sizes;
        TPParameter *data_pack_factor_inter;
        std::shared_ptr<TPExpr> data_pack_factor_intra;
        std::shared_ptr<TPExpr> compute_size();
        ~TPArrayTile() {
            for (auto lb : lbs) {
                delete lb;
            }
            for (auto size : sizes) {
                delete size;
            }
        }
};

class TuningProgram {
    public:
        TuningProgram(){id2 = -1;};
        /* Initialize the tuning program from an ISL schedule */
        __isl_give isl_schedule *init_from_schedule(__isl_take isl_schedule *schedule);
        __isl_give isl_schedule_node *tile(__isl_take isl_schedule_node *node, int div, std::string step);
        __isl_give isl_schedule_node *tile(
            __isl_take isl_schedule_node *node, int pos, int div, std::string step, std::unordered_set<std::string> tags, int bound);
        void dump(std::string dir);
        __isl_give isl_schedule *generate_tuning_schedule(__isl_take isl_schedule *schedule);
        __isl_give isl_schedule *generate_io_tuning_schedule(__isl_take isl_schedule *schedule, int io_level);
        void extract_module_loop_info(std::string name, std::vector<isl_ast_node *> &tree);
        std::shared_ptr<TPExpr> extract_module_num(isl_ast_node *tree);
        //std::shared_ptr<TPExpr> extract_io_module_num(isl_ast_node *tree, int io_level);
        std::vector<std::shared_ptr<TPExpr>> extract_module_dims(isl_ast_node *tree);
        std::vector<std::shared_ptr<TPExpr>> extract_module_dims_io(isl_ast_node *tree, int io_level);
        void extract_module_memory_info(std::string name, int double_buffer, TPArrayTile *tile, std::vector<isl_ast_node *> &tree);
        void extract_module_compute_info(std::string name, std::string arr_type, isl_ast_node *tree);
        void extract_module_io_info(std::string name, int io_level, std::vector<isl_ast_node *> &tree);
        void extract_module_attr(std::string name, int double_buffer, int in, int io, int to_dram, int serialize, int to_pe, int filter);
        std::shared_ptr<TPArrayRef> build_array_ref(std::string name, __isl_keep isl_map *ref, __isl_keep isl_schedule *);
        void update_tiled_arrays(TPIterator *tile_iter, TPIterator *point_iter, TPParameter *tile_factor);
        TPArrayTile *infer_tiled_array_bounds(TPArrayTile *tile, std::vector<std::shared_ptr<TPArrayRef>> refs, std::vector<TPIterator *> fixed_iters);
        std::vector<TPExpr *> infer_tiled_array_bound_at_dim(int dim, std::vector<std::shared_ptr<TPArrayRef>> refs, std::vector<TPIterator *> fixed_iters);
        TPExpr *infer_array_index_lb(TPExpr *, std::vector<TPIterator *> fixed_iters);
        TPExpr *infer_array_index_ub(TPExpr *, std::vector<TPIterator *> fixed_iters);
        void load_param_names(char *path);

        std::vector<TPIterator *> iters;        
        std::vector<TPParameter *> params;                
        std::vector<TPArray *> arrays;
        // Maps the parameter name to the point in "params"
        std::unordered_map<std::string, TPParameter *> param_map;        
        // kernel id to the tuning program
        int id;
        // second-level id for loop permutation
        int id2;
        std::unordered_map<std::string, std::shared_ptr<json>> module_loop_info;        
        std::unordered_map<std::string, std::shared_ptr<json>> module_memory_info;
        std::unordered_map<std::string, std::shared_ptr<json>> module_compute_info;
        std::unordered_map<std::string, std::shared_ptr<json>> module_io_info;
        std::unordered_map<std::string, std::shared_ptr<json>> module_attr;
        std::vector<std::string> param_names;
        std::unordered_map<std::string, int> param_names_cnt;

        ~TuningProgram() {                        
            for (int i = 0; i < iters.size(); i++)
                delete iters[i];            
            for (int i = 0; i < params.size(); i++)
                delete params[i];     
            for (int i = 0; i < arrays.size(); i++)        
                delete arrays[i];
        }

        // Future use
        //std::unordered_set<TPStatement *> stmts;
        //std::vector<TPTransformHistory *> transform_history;
        //std::unordered_map<TPIterator *, TPIterator *> iter_map;
        //std::unordered_map<TPStatement *, TPStatement *> stmt_map;
};

#if defined(__cplusplus)
}
#endif  

#endif