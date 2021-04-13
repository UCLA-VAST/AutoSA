#ifndef _AUTOSA_TUNING_H
#define _AUTOSA_TUNING_H

#include <isl/schedule.h>
#include <isl/schedule_node.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

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
        TPExpr() {}
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
        virtual std::string to_str();
        std::string func; // [floor, ceil, div, literal]
        std::vector<TPExpr *> ops;        
        
        ~TPExpr() {
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
        std::string name;
        TPExpr *lb;
        TPExpr *ub;     
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
            div = -1;
            dep_param = NULL;
            dep_iter = NULL;
            tune = false;
        }     
        TPParameter(TPParameter *p) {
            name = p->name;
            type = p->type;
            div = p->div;
            dep_param = p->dep_param;
            dep_iter = p->dep_iter;
            tune = p->tune;            
        }     
        std::string name;
        std::string type;        
        std::vector<TPExpr *> bounds;
        int div;
        TPParameter *dep_param;  
        TPIterator *dep_iter;
        bool tune;
        ~TPParameter(){
            for (int i = 0; i < bounds.size(); i++)
                delete bounds[i];
        }
};

class TPConst: public TPExpr {
    public:
        TPConst() {}
        TPConst(int v) {
            type = "const";
            val = v;
        }
        std::string type;
        int val;
};

class TuningProgram {
    public:
        TuningProgram(){};
        /* Initialize the tuning program from an ISL schedule */
        __isl_give isl_schedule *init_from_schedule(__isl_take isl_schedule *schedule);
        __isl_give isl_schedule_node *tile(__isl_take isl_schedule_node *node, int div);
        __isl_give isl_schedule_node *tile(__isl_take isl_schedule_node *node, int pos, int div);
        void dump(std::string dir);

        std::vector<TPIterator *> iters;
        std::vector<TPParameter *> params;        
        // Maps the parameter name to the point in "params"
        std::unordered_map<std::string, TPParameter *> param_map;        
        int id;

        ~TuningProgram() {                        
            for (int i = 0; i < iters.size(); i++)
                delete iters[i];            
            for (int i = 0; i < params.size(); i++)
                delete params[i];             
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