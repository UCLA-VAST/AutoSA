/* This function defines all the functions used for AutoSA tuning.
 * When executed in the tuning mode, AutoSA will automatically optimize the program,
 * applying different permutation and tiling techniques.
 * The program transform history and program loop structure are recorded, which 
 * are later used by the auto-tuner.
 */
#include <iomanip>
#include <iostream>
#include <fstream>

#include "ppcg.h"
#include "autosa_tuning.h"
#include "autosa_schedule_tree.h"

__isl_give TPExpr *TPExpr::div_by_param(__isl_take TPExpr *divisor) {        
    TPExpr *expr = new TPExpr("div", this, divisor);
    return expr;
}

__isl_give TPExpr *TPExpr::ceil() {    
    TPExpr *expr = new TPExpr("ceil", this);
    return expr;
}

__isl_give TPExpr *TPExpr::add(__isl_take TPExpr *expr) {
    if (this->func == "NULL") {        
        delete this;
        return expr;        
    } else {
        TPExpr *new_expr = new TPExpr("add", this, expr);
        return new_expr;
    }
}

__isl_give TPExpr *TPExpr::mul(__isl_take TPExpr *expr) {   
    if (this->func == "NULL") {
        delete this;
        return expr;
    } else if (this->to_str() == "1") {
        delete this;
        return expr;
    } else if (expr->to_str() == "1") {
        delete expr;
        return this;
    } else {
        TPExpr *new_expr = new TPExpr("mul", this, expr);
        return new_expr;    
    }
}

__isl_give TPExpr *TPExpr::subtract(__isl_take TPExpr *expr) {    
    if (this->func == "literal" && dynamic_cast<TPConst *>(this->ops[0])) {        
        int val = ((TPConst *)(this->ops[0]))->val;
        if (expr->func == "literal" && dynamic_cast<TPConst *>(expr->ops[0])) {
            val -= ((TPConst *)(expr->ops[0]))->val;        
            delete this;
            delete expr;
            return new TPExpr("literal", new TPConst(val));
        }
    } else if (expr->func == "literal" && dynamic_cast<TPConst *>(expr->ops[0])) {
        int val = ((TPConst *)(expr->ops[0]))->val;
        if (val == 0) {
            delete expr;
            return this;
        }        
    }
    TPExpr *new_expr = new TPExpr("sub", this, expr);
    return new_expr;
}

__isl_give TPExpr *TPExpr::min(__isl_take TPExpr *expr) {    
    if (this->func == "literal" && dynamic_cast<TPConst *>(this->ops[0])) {        
        int val = ((TPConst *)(this->ops[0]))->val;
        if (expr->func == "literal" && dynamic_cast<TPConst *>(expr->ops[0])) {
            val = std::min(val, ((TPConst *)(expr->ops[0]))->val);
            delete this;
            delete expr;
            return new TPExpr("literal", new TPConst(val));
        }
    } else if (this->func == "NULL") {
        delete this;
        return expr;
    } else if (this->to_str() == expr->to_str()) {
        delete expr;
        return this;
    }
    TPExpr *new_expr = new TPExpr("min", this, expr);
    return new_expr;
}

__isl_give TPExpr *TPExpr::max(__isl_take TPExpr *expr) {
    if (this->func == "literal" && dynamic_cast<TPConst *>(this->ops[0])) {        
        int val = ((TPConst *)(this->ops[0]))->val;
        if (expr->func == "literal" && dynamic_cast<TPConst *>(expr->ops[0])) {
            val = std::max(val, ((TPConst *)(expr->ops[0]))->val);
            delete this;
            delete expr;
            return new TPExpr("literal", new TPConst(val));
        }
    } else if (this->func == "NULL") {
        delete this;
        return expr;
    } else if (this->to_str() == expr->to_str()) {
        delete expr;
        return this;
    }
    TPExpr *new_expr = new TPExpr("max", this, expr);
    return new_expr;
}

/* Create a duplicate of the current expression. */
__isl_give TPExpr *TPExpr::dup() {
    TPExpr *new_expr = new TPExpr();
    new_expr->func = this->func;
    if (this->func == "literal") {
        TPExpr *op = this->ops[0];
        if (dynamic_cast<TPParameter *>(op)) {            
            new_expr->ops.push_back(((TPParameter *)(op))->dup());
        } else if (dynamic_cast<TPConst *>(op)) {            
            new_expr->ops.push_back(((TPConst *)(op))->dup());            
        }
    } else {
        for (auto op : this->ops) {
            new_expr->ops.push_back(op->dup());
        }
    }
    return new_expr;
}        

__isl_give TPParameter *TPParameter::dup() {
    TPParameter *new_param = new TPParameter();
    new_param->name = this->name;
    new_param->name_prefix = this->name_prefix;
    new_param->type = this->type;
    for (auto bound : this->bounds) {
        new_param->bounds.push_back(std::shared_ptr<TPExpr>(bound->dup()));
    }    
    for (auto d : this->divisors) {
        new_param->divisors.push_back(std::shared_ptr<TPExpr>(d->dup()));
    }    
    for (auto m : this->multiples) {
        new_param->multiples.push_back(std::shared_ptr<TPExpr>(m->dup()));
    }    
    new_param->tune = this->tune;    
    new_param->attr = this->attr; 
    for (auto tag : this->tags) {
        new_param->tags.insert(tag);
    }

    return new_param;
}

__isl_give TPConst *TPConst::dup() {
    TPConst *new_const = new TPConst();
    new_const->type = this->type;
    new_const->val = this->val;

    return new_const; 
}

bool propagate_cst(TPExpr *expr, int cst) {
    bool status = false;
    if (expr->func == "add" || expr->func == "sub") {
        if (expr->ops[1]->func == "add" || expr->ops[1]->func == "sub") {
            status = propagate_cst(expr->ops[1], cst);
        } else if (expr->ops[1]->func == "literal" && dynamic_cast<TPConst *>(expr->ops[1]->ops[0])) {
            int new_cst;
            if (expr->func == "sub") 
                new_cst = dynamic_cast<TPConst *>(expr->ops[1]->ops[0])->val - cst;
            else
                new_cst = dynamic_cast<TPConst *>(expr->ops[1]->ops[0])->val + cst;
            delete expr->ops[1]->ops[0];
            expr->ops[1]->ops[0] = new TPConst(new_cst);
            status = true;
        }
    }
    return status;
}

__isl_give TPExpr *const_propagation(__isl_take TPExpr *expr) {
    TPExpr *ret_expr = expr;
    if (ret_expr->func == "add" || ret_expr->func == "sub") {
        /* Check if const propogation is possible */
        if (ret_expr->ops[1]->func == "literal" && dynamic_cast<TPConst *>(ret_expr->ops[1]->ops[0])) {            
            bool status = propagate_cst(ret_expr->ops[0], dynamic_cast<TPConst *>(ret_expr->ops[1]->ops[0])->val);
            if (status) {                
                TPExpr *new_expr = ret_expr->ops[0]->dup();                
                delete ret_expr;
                ret_expr = new_expr;
            }
        }
        /* Check if there is any zero in the operands. */
        if (ret_expr->ops[1]->func == "literal" && dynamic_cast<TPConst *>(ret_expr->ops[1]->ops[0])) {
            if (dynamic_cast<TPConst *>(ret_expr->ops[1]->ops[0])->val == 0) {
                TPExpr *new_expr = ret_expr->ops[0]->dup();
                delete ret_expr;
                ret_expr = new_expr;
            }
        }        
    }
    for (int i = 0; i < ret_expr->ops.size(); i++) {
        ret_expr->ops[i] = ret_expr->ops[i]->simplify();
    }
    return ret_expr;
}

__isl_give TPExpr *combine_like_terms(__isl_take TPExpr *expr) {
    TPExpr *ret_expr = expr;

    if (ret_expr->func == "add" || ret_expr->func == "sub") {
        /* Try unite like terms */
        //if (ret_expr->ops[0]->func == "mul") {
        //    std::cout << "f1: " << ret_expr->ops[0]->ops[1]->to_str() << std::endl;
        //    std::cout << "f2: " << ret_expr->ops[1]->to_str() << std::endl;
        //}
        if (ret_expr->ops[0]->func == "mul" && 
            (ret_expr->ops[0]->ops[1]->to_str() == ret_expr->ops[1]->to_str())) {
            TPExpr *left = ret_expr->ops[0]->ops[0]->dup();
            TPExpr *right = ret_expr->ops[0]->ops[1]->dup();
            if (ret_expr->func == "add") {
                left = left->add(new TPExpr("literal", new TPConst(1)));
            } else {
                left = left->subtract(new TPExpr("literal", new TPConst(1)));
            }
            TPExpr *new_expr = new TPExpr("mul", left, right);
            delete ret_expr;
            ret_expr = new_expr;
        }
    }

    ret_expr = const_propagation(ret_expr);

    return ret_expr;
}

__isl_give TPExpr *simplify_chain_ops(__isl_take TPExpr *expr) {
    TPExpr *ret_expr = expr;

    if (ret_expr->func == "mul") {
        if (ret_expr->ops[0]->func == "div" &&
            (ret_expr->ops[0]->ops[1]->to_str() == ret_expr->ops[1]->to_str())) {
            TPExpr *new_expr = ret_expr->ops[0]->ops[0]->dup();
            delete ret_expr;
            ret_expr = new_expr;
        }
    }

    return ret_expr;
}

/* Simplify the expression. */
__isl_give TPExpr *TPExpr::simplify() {
    TPExpr *ret_expr = this;
    /* Const propagation */
    ret_expr = const_propagation(ret_expr);
    /* Combine like terms */
    ret_expr = combine_like_terms(ret_expr); 
    /* Simplify chain ops */
    ret_expr = simplify_chain_ops(ret_expr);

    return ret_expr;
}

/* Replace the expression that matches "match" with replace.
 */
__isl_give TPExpr *TPExpr::replace(__isl_keep TPExpr *match, __isl_keep TPExpr *replace) {
    if (this->to_str() == match->to_str()) {
        /* Matched */
        delete this;
        return replace->dup();
    } else {
        if (this->func == "literal") {
            return this;
        } else if (this->func == "floor" || this->func == "ceil") {
            this->ops[0] = this->ops[0]->replace(match, replace);        
            return this;
        } else if (this->func == "div" || this->func == "add" || this->func == "mul" || 
                   this->func == "min" || this->func == "max" || this->func == "sub") {
            this->ops[0] = this->ops[0]->replace(match, replace);
            this->ops[1] = this->ops[1]->replace(match, replace);
            return this;
        } else if (this->func == "NULL") {
            return this;
        } else {
            std::cout << "[AutoSA] Error: TPExpr::replace(): Unsupported TPExpr function type: " << this->func << std::endl;
            exit(1);
        }
    }
}

std::string TPExpr::to_str() {
    if (this->func == "literal") {
        TPExpr *op = this->ops[0];        
        if (dynamic_cast<TPParameter *>(op)) {            
            return ((TPParameter *)(op))->name;
        } else if (dynamic_cast<TPConst *>(op)) {            
            return std::to_string(((TPConst *)(op))->val);
        }
    } else if (this->func == "floor") {        
        std::string ret = "floor(";
        ret += this->ops[0]->to_str();
        ret += ")";
        return ret;
    } else if (this->func == "ceil") {
        std::string ret = "ceil(";
        ret += this->ops[0]->to_str();
        ret += ")";
        return ret;
    } else if (this->func == "div") {
        int single_op = 0;
        std::string l = this->ops[0]->to_str();        
        std::string r = this->ops[1]->to_str();
        if (r == "1")
            single_op = 1;            
        std::string ret = "";
        if (!single_op)
            ret += "(";
        ret += l;        
        if (r != "1") {
            ret += ("/" + r);
        }
        if (!single_op)
            ret += ")";        
        return ret;
    } else if (this->func == "add") {        
        std::string l = this->ops[0]->to_str();        
        std::string r = this->ops[1]->to_str();
        std::string ret = "(" + l + "+" + r + ")";
        return ret;
    } else if (this->func == "sub") {        
        std::string l = this->ops[0]->to_str();        
        std::string r = this->ops[1]->to_str();
        std::string ret = "(" + l + "-" + r + ")";
        return ret;
    } else if (this->func == "mul") {
        int single_op = 0;        
        std::string l = this->ops[0]->to_str();        
        std::string r = this->ops[1]->to_str();
        if (l == "1" || r == "1")
            single_op = 1;
        std::string ret = "";
        if (!single_op)
            ret += "(";
        if (l != "1")
            ret += l;
        if (l != "1" && r != "1")
            ret += "*";
        if (r != "1")
            ret += r;        
        if (!single_op)
            ret += ")";
        return ret;    
    } else if (this->func == "min") {        
        std::string l = this->ops[0]->to_str();        
        std::string r = this->ops[1]->to_str();
        std::string ret = "min(" + l + "," + r + ")";
        return ret;
    } else if (this->func == "max") {        
        std::string l = this->ops[0]->to_str();        
        std::string r = this->ops[1]->to_str();
        std::string ret = "max(" + l + "," + r + ")";
        return ret;
    } else if (this->func == "NULL") {
        return "";
    } else {
        std::cout << "[AutoSA] Error: TPExpr::to_str(): Unsupported TPExpr function type: " << this->func << std::endl;
        exit(1);
    }
    return "";
}

std::string TPParameter::to_str() {
    return this->name;
}

__isl_give TPExpr *TPExpr::infer_bound(
    std::unordered_map<std::string, TPExpr *> lbs, 
    std::unordered_map<std::string, TPExpr *> ubs,
    std::unordered_set<std::string> ignore, int max)
{    
    if (this->func == "literal") {
        TPExpr *op = this->ops[0];
        if (dynamic_cast<TPParameter *>(op)) {          
            TPParameter *param = (TPParameter *)(op);            
            if (ignore.find(param->name) != ignore.end()) {
                return new TPExpr("literal", new TPConst(0));
            } else if (lbs.find(param->name) != lbs.end() || ubs.find(param->name) != ubs.end()){
                if (max == 1) {
                    return (ubs[param->name]->dup())->subtract(new TPExpr("literal", new TPConst(1)));
                } else {                    
                    return lbs[param->name]->dup();
                }
            } else {
                return this->dup();
            }
        } else if (dynamic_cast<TPConst *>(op)) {                        
            return this->dup();
        }
    } else if (this->func == "floor") {
        std::cout << "[AutoSA] Error: TPExpr::infer_bound(): Unsupported TPExpr function type: " << this->func << std::endl;
        exit(1);
    } else if (this->func == "ceil") {
        std::cout << "[AutoSA] Error: TPExpr::infer_bound(): Unsupported TPExpr function type: " << this->func << std::endl;
        exit(1);
    } else if (this->func == "div") {
        std::cout << "[AutoSA] Error: TPExpr::infer_bound(): Unsupported TPExpr function type: " << this->func << std::endl;
        exit(1);
    } else if (this->func == "add") {
        TPExpr *left, *right;
        if (max == 1) {
            left = this->ops[0]->infer_bound(lbs, ubs, ignore, 1);
            right = this->ops[1]->infer_bound(lbs, ubs, ignore, 1);
        } else {
            left = this->ops[0]->infer_bound(lbs, ubs, ignore, 0);
            right = this->ops[1]->infer_bound(lbs, ubs, ignore, 0);
        }
        if (left->to_str() == "0" && right->to_str() == "0") {
            delete left;
            delete right;
            return new TPExpr("literal", new TPConst(0));
        } else if (left->to_str() == "0") {
            delete left;
            return right;
        } else if (right->to_str() == "0") {
            delete right;
            return left;
        } else {
            return new TPExpr("add", left, right);
        }
    } else if (this->func == "mul") {
        TPExpr *left, *right;
        if (max == 1) {
            left = this->ops[0]->infer_bound(lbs, ubs, ignore, 1);
            right = this->ops[1]->infer_bound(lbs, ubs, ignore, 1);
        } else {
            left = this->ops[0]->infer_bound(lbs, ubs, ignore, 0);
            right = this->ops[1]->infer_bound(lbs, ubs, ignore, 0);
        }
        if (left->to_str() == "0" || right->to_str() == "0") {
            delete left;
            delete right;
            return new TPExpr("literal", new TPConst(0));
        } else
            return new TPExpr("mul", left, right);
    } else {
        std::cout << "[AutoSA] Error: TPExpr::infer_bound(): Unsupported TPExpr function type: " << this->func << std::endl;
        exit(1);
    }
    return NULL;
}

std::string TPArrayRef::to_str() {
    std::string ret = this->name;
    for (auto index : this->index) {
        ret += ("[" + index->to_str() + "]");                
    }
    return ret;
}

static __isl_give isl_schedule_node *extract_tuning_program_from_schedule(
    __isl_take isl_schedule_node *node, void *user)
{
    if (!node)
        return NULL;
    
    TuningProgram *prog = (TuningProgram *)user;

    if (isl_schedule_node_get_type(node) == isl_schedule_node_band) 
    {        
        int n = isl_schedule_node_band_n_member(node);        
        for (int i = 0; i < n; i++) {            
            /* We assume the loop bounds are independent and 
             * all the loops start from zero for now. 
             */                        
            TPParameter *ub;            
            if (prog->param_names.size() > 0) {
                // Use the pre-assigned parameter names
                ub = new TPParameter(prog->param_names[prog->params.size()], 0);
                //std::cout << prog->params.size() << std::endl;
                //std::cout << prog->param_names[prog->params.size()] << std::endl;
                prog->param_names_cnt[ub->name] = 1;
            } else {
                ub = new TPParameter("p" + std::to_string(prog->params.size()));
            }
            prog->params.push_back(ub);
            prog->param_map[ub->name] = ub;
            ub->tune = false;
            ub->attr = "loop_ub";
            ub->tags.insert("external");

            TPIterator *iter = new TPIterator(
                "c" + std::to_string(prog->iters.size()),                
                new TPExpr("literal", new TPConst(0)),
                new TPExpr("literal", new TPParameter(ub)));            
            // Assign the iterator to schedule dim                        
            node = isl_schedule_node_band_member_set_iter(node, i, (void *)iter);            
            prog->iters.push_back(iter);
        }                
    }

    return node;
}

/* Initialize the tuning program from the schedule. 
 * We will bind all the band dimensions in the schedule with an iterator variable to keep then in track.
 * All the future transformations on the band dimensions will also be recored by the tuning program.
 */
__isl_give isl_schedule *TuningProgram::init_from_schedule(__isl_take isl_schedule *schedule) {
    // Init the iter field to each dim of the schedule tree
    // TODO: Add a legality check.
    // Currently, we require all axis to be independent of each other. And the loop iterators
    // should start from 0.
    isl_schedule_node *root = isl_schedule_get_root(schedule);
    root = isl_schedule_node_map_descendant_bottom_up(root, 
                                                      &extract_tuning_program_from_schedule, this);
    isl_schedule_free(schedule);
    schedule = isl_schedule_node_get_schedule(root);
    isl_schedule_node_free(root);

    return schedule;
}

/* Load the customized parameter names. */
void TuningProgram::load_param_names(char *path) {
    if (path == NULL)
        return;
    std::ifstream i(path);
    json namings;
    i >> namings;
    std::string kernel_name = "kernel" + std::to_string(this->id);    
    auto kernel_names = namings[kernel_name];    
    for (std::string n : kernel_names) {
        this->param_names.push_back(n);        
    }    
}

/* Update the band iters after tiling. The "node" points to the tile band. 
 * Div indicates if the tiling factors should be a divisor of the tiled loop.
 */
__isl_give isl_schedule_node *TuningProgram::tile(__isl_take isl_schedule_node *node, int div, std::string step)
{    
    isl_schedule_node *tile_node = node;
    isl_schedule_node *point_node = isl_schedule_node_child(isl_schedule_node_copy(node), 0);
    int n = isl_schedule_node_band_n_member(point_node);
    for (int i = 0; i < n; i++) {                
        /* We assume all the loops start from zero for now. */
        TPIterator *tile_iter = (TPIterator *)isl_schedule_node_band_member_get_iter(tile_node, i);
        TPParameter *tile_ub = (TPParameter *)(tile_iter->ub->ops[0]);
        /* Check if the parameter name is customized. 
         * If so, following the same naming fashion.
         */
        TPParameter *point_ub;
        if (this->param_names.size() > 0) {
            //std::cout << tile_ub->name_prefix << std::endl;
            point_ub = new TPParameter(tile_ub->name_prefix, this->param_names_cnt[tile_ub->name_prefix]);
            this->param_names_cnt[tile_ub->name_prefix] += 1;
        } else {
            point_ub = new TPParameter("p" + std::to_string(this->params.size()));
        }
        point_ub->tune = true;
        //point_ub->div = div;
        point_ub->bounds.push_back(std::make_shared<TPExpr>("literal", new TPConst(1)));        
        this->param_map[tile_ub->to_str()]->split_by = point_ub;
        point_ub->bounds.push_back(std::make_shared<TPExpr>("literal", new TPParameter(tile_ub)));        
        if (div) {
            point_ub->divisors.push_back(std::make_shared<TPExpr>("literal", new TPParameter(tile_ub)));
        }
        point_ub->attr = step + "_tiling_factor";
        this->params.push_back(point_ub);
        this->param_map[point_ub->name] = point_ub;
                
        // Update the loop bound
        if (div == 0)
            tile_iter->ub = (tile_iter->ub->div_by_param(new TPExpr("literal", new TPParameter(point_ub))))->ceil();
        else
            tile_iter->ub = tile_iter->ub->div_by_param(new TPExpr("literal", new TPParameter(point_ub)));

        // Point loop                        
        TPIterator *point_iter = new TPIterator(
            "c" + std::to_string(this->iters.size()), 
            new TPExpr("literal", new TPConst(0)), 
            new TPExpr("literal", new TPParameter(point_ub)));
        if (isl_schedule_node_band_member_get_space_time(point_node, i) == autosa_loop_space) {
            //std::cout << "iter space: " << point_iter->name << std::endl;
            point_iter->space_time = "space";
        } else {
            point_iter->space_time = "time";        
        }
        point_node = isl_schedule_node_band_member_set_iter(point_node, i, (void *)point_iter);
        this->iters.push_back(point_iter);

        // Update the array indices
        this->update_tiled_arrays(tile_iter, point_iter, point_ub);
    }

    isl_schedule_node_free(tile_node);
    node = isl_schedule_node_parent(point_node);    

    return node;
}

/* Update the band iters after tiling. The "node" points to the tile band. 
 * Dim "pos" in the band is tiled. Point band contains a single loop.
 */
__isl_give isl_schedule_node *TuningProgram::tile(
    __isl_take isl_schedule_node *node, int pos, int div, std::string step, std::unordered_set<std::string> tags, int bound)
{    
    isl_schedule_node *tile_node = node;
    isl_schedule_node *point_node = isl_schedule_node_child(isl_schedule_node_copy(node), 0);    
    TPIterator *tile_iter = (TPIterator *)isl_schedule_node_band_member_get_iter(tile_node, pos);
    //std::cout << step << " " << tile_iter->name << " " << tile_iter->space_time << std::endl;
    TPParameter *tile_ub = (TPParameter *)(tile_iter->ub->ops[0]);
    //TPParameter *point_ub = new TPParameter("p" + std::to_string(this->params.size()));
    TPParameter *point_ub;
    if (this->param_names.size() > 0) {
        point_ub = new TPParameter(tile_ub->name_prefix, this->param_names_cnt[tile_ub->name_prefix]);
        this->param_names_cnt[tile_ub->name_prefix] += 1;
    } else {
        point_ub = new TPParameter("p" + std::to_string(this->params.size()));
    }
    point_ub->tune = true;
    point_ub->bounds.push_back(std::make_shared<TPExpr>("literal", new TPConst(1)));    
    this->param_map[tile_ub->to_str()]->split_by = point_ub;
    point_ub->bounds.push_back(std::make_shared<TPExpr>("literal", new TPParameter(tile_ub)));    
    if (step == "SIMD") {
        point_ub->bounds[1] = std::shared_ptr<TPExpr>(point_ub->bounds[1]->dup()->min(new TPExpr("literal", new TPConst(bound))));
    }

    point_ub->attr = step + "_tiling_factor";
    for (auto tag : tags) {
        point_ub->tags.insert(tag);
    }

    if (div) 
        point_ub->divisors.push_back(std::make_shared<TPExpr>("literal", new TPParameter(tile_ub)));
    this->params.push_back(point_ub);
    this->param_map[point_ub->name] = point_ub;
            
    // Update the loop bound
    if (div == 0)
        tile_iter->ub = (tile_iter->ub->div_by_param(new TPExpr("literal", new TPParameter(point_ub))))->ceil();
    else
        tile_iter->ub = tile_iter->ub->div_by_param(new TPExpr("literal", new TPParameter(point_ub)));

    // Point loop                        
    TPIterator *point_iter = new TPIterator(
        "c" + std::to_string(this->iters.size()), 
        new TPExpr("literal", new TPConst(0)), 
        new TPExpr("literal", new TPParameter(point_ub)));    
    if (isl_schedule_node_band_member_get_space_time(point_node, 0) == autosa_loop_space) {
        point_iter->space_time = "space";
    } else {
        point_iter->space_time = "time";        
    }        
    point_node = isl_schedule_node_band_member_set_iter(point_node, 0, (void *)point_iter);
    this->iters.push_back(point_iter);    

    isl_schedule_node_free(tile_node);
    node = isl_schedule_node_parent(point_node);

    // Update the array indices
    this->update_tiled_arrays(tile_iter, point_iter, point_ub);

    return node;
}

/* Dump out the tuning program information to a JSON file. 
 */
void TuningProgram::dump(std::string dir)
{
    json j;
    // params
    json j_params;
    for (int i = 0; i < this->params.size(); i++) {
        json j_param;
        TPParameter *param = this->params[i];
        j_param["name"] = param->name;       
        if (param->split_by)  {
            j_param["split_by"] = param->split_by->to_str();
        }
        for (auto d : param->divisors) {
            j_param["divisors"].push_back(d->to_str());
        }        
        for (auto m : param->multiples) {
            j_param["multiples"].push_back(m->to_str());
        }
        j_param["tunable"] = param->tune;
        j_param["attr"] = param->attr;    
        if (param->bounds.size() > 0)
            j_param["bounds"] = {param->bounds[0]->to_str(), param->bounds[1]->to_str()};        
        for (auto tag : param->tags) {
            j_param["tags"].push_back(tag);
        }
        j_params.push_back(j_param);
    }
    j["params"] = j_params;

    // loop struct - latency    
    for (auto x: this->module_loop_info) {
        //std::cout << x.first << std::endl;
        j["latency"][x.first] = *x.second;
    }
    
    // design stats - resource
    for (auto x: this->module_memory_info) {
        j["memory"][x.first] = *x.second;
    }
    for (auto x: this->module_compute_info) {        
        j["compute"][x.first] = *x.second;
    }
    for (auto x: this->module_io_info) {
        j["io"][x.first] = *x.second;
    }

    for (auto x: this->module_attr) {
        j["attr"][x.first] = *x.second;
    }

    std::string file_name = dir + "/kernel" + std::to_string(this->id);
    if (this->id2 >= 0) {
        file_name += "_";        
        file_name += std::to_string(this->id2);
    }
    std::ofstream o(file_name + ".json");
    o << std::setw(4) << j << std::endl;
    o.close();    

    return;
}

/* Break all band node into single bands, add a comment marker containing the 
 * corresponding TPIterator pointer.
 */
static __isl_give isl_schedule_node *modify_tuning_schedule(
    __isl_take isl_schedule_node *node, void *user)
{
    if (!node)
        return NULL;

    TuningProgram *program = (TuningProgram *)user;
    isl_ctx *ctx = isl_schedule_node_get_ctx(node);    

    if (isl_schedule_node_get_type(node) == isl_schedule_node_band) {
        int n = isl_schedule_node_band_n_member(node);
        for (int i = n - 1; i >= 0; i--) {
            if (i > 0) {
                node = isl_schedule_node_band_split(node, i);
                node = isl_schedule_node_child(node, 0);
            }
            TPIterator *iter = (TPIterator *)isl_schedule_node_band_member_get_iter(node, 0);
            //if (iter) {
            //    std::cout << iter->name << std::endl;
            //    std::cout << iter->space_time << std::endl;
            //}
            if (iter) {
                isl_id *id = isl_id_alloc(ctx, "iter_info", iter);
                /* Insert it under the current band node. */
                node = isl_schedule_node_child(node, 0);
                node = isl_schedule_node_insert_mark(node, id);
                node = isl_schedule_node_parent(node); // band node
            }
            if (i > 0) {
                node = isl_schedule_node_parent(node);
            }
        }
        //node = isl_schedule_node_parent(node);
    }

    return node;
}

/* This function generates a new schedule used for performance estimation.
 * Specially, all the band dims are broken into single band, and a new mark node is added above 
 * each band, which contains the detailed information of the loop iterator.
 */
__isl_give isl_schedule *TuningProgram::generate_tuning_schedule(__isl_take isl_schedule *schedule) {
    isl_schedule *new_schedule = isl_schedule_dup(schedule);
    isl_schedule_free(schedule);    

    isl_schedule_node *root = isl_schedule_get_root(new_schedule);    
    root = isl_schedule_node_map_descendant_bottom_up(root,
                                                      &modify_tuning_schedule, this);

    isl_schedule_free(new_schedule);
    new_schedule = isl_schedule_node_get_schedule(root);
    isl_schedule_node_free(root);    
    
    return new_schedule;
}

std::shared_ptr<json> extract_isl_ast_node_user(__isl_keep isl_ast_node *node)
{
    isl_ctx *ctx = isl_ast_node_get_ctx(node);
    isl_ast_expr *expr = isl_ast_node_user_get_expr(node);
    isl_printer *p_str = isl_printer_to_str(ctx);
    p_str = isl_printer_set_output_format(p_str, ISL_FORMAT_C);
    p_str = isl_printer_print_ast_expr(p_str, expr);
    char *user_expr = isl_printer_get_str(p_str);
    isl_printer_free(p_str);

    std::shared_ptr<json> info = std::make_shared<json>();
    std::string user_expr_str(user_expr);
    (*info)["user_expr"] = user_expr_str;

    free(user_expr);
    isl_ast_expr_free(expr);

    return info;
}

struct extract_loop_info_data {
    int after_for;
};

std::shared_ptr<json> extract_loop_info(__isl_keep isl_ast_node *node, void *user)
{    
    std::shared_ptr<json> j_info;
    enum isl_ast_node_type type;    
    isl_ctx *ctx = isl_ast_node_get_ctx(node);
    type = isl_ast_node_get_type(node);
    struct extract_loop_info_data *data = (struct extract_loop_info_data *)user;

    switch(type) {
        case isl_ast_node_for:
        {         
            data->after_for = 1;            
            isl_ast_node *child;
            child = isl_ast_node_for_get_body(node);
            std::shared_ptr<json> j_child = extract_loop_info(child, user);
            isl_ast_node_free(child);
            j_info = j_child;            

            break;
        }
        case isl_ast_node_block:
        {
            data->after_for = 0;
            /* Extract the block information and insert it into the loop struc. */
            j_info = std::make_shared<json>();
            *j_info = {{"type", "block"}, {"child", {}}};
            isl_ast_node_list *child_list = isl_ast_node_block_get_children(node);
            int n_child = isl_ast_node_list_n_ast_node(child_list);
            for (int i = 0; i < n_child; i++) {
                isl_ast_node *child = isl_ast_node_list_get_ast_node(child_list, i);
                std::shared_ptr<json> j_child = extract_loop_info(child, user);
                isl_ast_node_free(child);
                (*j_info)["child"].push_back(*j_child);
            }
            isl_ast_node_list_free(child_list);            
            break;
        }
        case isl_ast_node_user:
        {
            data->after_for = 0;
            /* Print nothing. */
            j_info = std::make_shared<json>();
            std::shared_ptr<json> j_user = extract_isl_ast_node_user(node);
            *j_info = {{"type", "user"}, {"child", *j_user}};            
            break;
        }
        case isl_ast_node_if: 
        {
            data->after_for = 0;
            j_info = std::make_shared<json>();
            *j_info = {{"type", "if"}, {"child", {}}};
            isl_ast_node *then_child, *else_child;
            then_child = isl_ast_node_if_get_then_node(node);
            std::shared_ptr<json> j_then = extract_loop_info(then_child, user);
            isl_ast_node_free(then_child);
            (*j_info)["child"].push_back(*j_then);

            else_child = isl_ast_node_if_get_else_node(node);
            if (else_child) {
                std::shared_ptr<json> j_else = extract_loop_info(else_child, user);
                isl_ast_node_free(else_child);
                (*j_info)["child"].push_back(*j_else);
            }            
            break;
        }
        case isl_ast_node_mark: 
        {            
            isl_id *id = isl_ast_node_mark_get_id(node);                        
            TPIterator *iter = NULL;
            if (!strcmp(isl_id_get_name(id), "iter_info")) {
                if (data->after_for == 1) {
                    /* For loop */                
                    isl_ast_node *child = isl_ast_node_mark_get_node(node);
                    data->after_for = 0;
                    std::shared_ptr<json> j_child = extract_loop_info(child, user);
                    isl_ast_node_free(child);
                    iter = (TPIterator *)isl_id_get_user(id);
                    if (iter) {
                        j_info = std::make_shared<json>();
                        *j_info = {{"type", "for"}, {"iterator", iter->name}};
                        (*j_info)["bounds"].push_back(iter->lb->to_str());                
                        (*j_info)["bounds"].push_back(iter->ub->to_str());
                        (*j_info)["child"] = *j_child;
                    } else {
                        j_info = j_child;
                    }  
                } else {
                    /* Skip this one */
                    isl_ast_node *child = isl_ast_node_mark_get_node(node);
                    std::shared_ptr<json> j_child = extract_loop_info(child, user);
                    isl_ast_node_free(child);
                    j_info = j_child;
                }                             
            } else if (!strcmp(isl_id_get_name(id), "tuning_array_tile")) {
                data->after_for = 0;
                /* Print the array information */
                TPArrayTile *tile = (TPArrayTile *)isl_id_get_user(id);
                j_info = std::make_shared<json>();
                *j_info = {{"type", "array_tile"}, {"data_pack_factor", tile->data_pack_factor_inter->name}};
                std::string size = "";
                int is_first = 1;
                for (auto s : tile->sizes) {
                    if (!is_first)
                        size += "*";
                    size += s->to_str();
                    is_first = 0;
                }
                (*j_info)["size"] = size;
                (*j_info)["ele_size"] = tile->ele_size;
                (*j_info)["last_dim"] = tile->sizes[tile->sizes.size() - 1]->to_str();
            } else {
                std::string mark_content(isl_id_get_name(id));
                j_info = std::make_shared<json>();
                *j_info = {{"type", "mark"}, {"content", mark_content}};
                isl_ast_node *child = isl_ast_node_mark_get_node(node);
                data->after_for = 0;
                std::shared_ptr<json> j_child = extract_loop_info(child, user);
                isl_ast_node_free(child);                
                (*j_info)["child"] = *j_child;
            }
            isl_id_free(id);                        

            break;
        }
        default:
        {
            data->after_for = 0;
            break;
        }
    }

    return j_info;
}

/* Extract the loop structure from the "ast", used for latency estimation.
 * TODO: Extract the hw information for resource estimation. 
 */
void TuningProgram::extract_module_loop_info(std::string name, std::vector<isl_ast_node *> &ast) 
{
    if (ast.size() == 0)
        return;
            
    if (ast.size() == 1) {
        std::shared_ptr<json> j_loop;    
        struct extract_loop_info_data data = {0};
        j_loop = extract_loop_info(ast[0], &data);
        this->module_loop_info[name] = j_loop;
    } else if (ast.size() == 3) {        
        // outer module
        std::shared_ptr<json> j_loop1;
        struct extract_loop_info_data data = {0};
        j_loop1 = extract_loop_info(ast[0], &data);
        this->module_loop_info[name] = j_loop1;
        // intra module
        std::shared_ptr<json> j_loop2;
        data.after_for = 0;
        j_loop2 = extract_loop_info(ast[1], &data);
        this->module_loop_info[name + "_intra"] = j_loop2;
        // inter module
        std::shared_ptr<json> j_loop3;
        data.after_for = 0;
        j_loop3 = extract_loop_info(ast[2], &data);
        this->module_loop_info[name + "_inter"] = j_loop3;
    }

    return;
}

void TuningProgram::extract_module_attr(
    std::string name, int double_buffer, int in, int io, int to_dram, int serialize, int to_pe, int filter) {
    std::shared_ptr<json> j = std::make_shared<json>();    
    (*j)["double_buffer"] = double_buffer;
    (*j)["in"] = in;
    (*j)["io"] = io;
    (*j)["to_dram"] = to_dram;
    (*j)["serialize"] = serialize;
    (*j)["to_pe"] = to_pe;
    (*j)["filter"] = filter;

    this->module_attr[name] = j;

    return;
}

struct build_dim_iter_map_data {
    isl_map *ref;
    isl_map *new_ref;
    std::unordered_map<int, TPIterator *> dim_iter_map;  
    TPExpr *dim_expr;
    int done;
};

/* Test if the partial schedule above the "node" matches the "domain".
 * If so, climb the schedule tree and update the mapping between the schedule dimension and the 
 * TPIterator.
 */
__isl_give isl_schedule_node *build_dim_iter_map(__isl_take isl_schedule_node *node, void *user)
{    
    struct build_dim_iter_map_data *data = (struct build_dim_iter_map_data *)user;
    if (data->done)
        return node;

    isl_union_set *domain = isl_schedule_node_get_domain(node);
    isl_union_set *ref_domain = isl_union_set_from_set(isl_map_domain(isl_map_copy(data->ref)));
    if (!isl_union_set_is_empty(domain) && isl_union_set_is_strict_subset(domain, ref_domain)) {                
        isl_union_map *prefix = isl_schedule_node_get_prefix_schedule_relation(node);
        data->new_ref = isl_map_from_union_map(isl_union_map_apply_domain(
            isl_union_map_from_map(isl_map_copy(data->ref)), prefix));            
        data->done = 1; 
        isl_schedule_node *new_node = isl_schedule_node_copy(node);
        while (isl_schedule_node_has_parent(new_node)) {
            if (isl_schedule_node_get_type(new_node) == isl_schedule_node_band) {
                isl_set *new_prefix_sched_domain = 
                    isl_set_from_union_set(isl_union_map_range(isl_schedule_node_get_prefix_schedule_relation(new_node)));
  
                int n = isl_schedule_node_band_n_member(new_node);
                for (int i = 0; i < n; i++) {
                    TPIterator *iter = (TPIterator *)isl_schedule_node_band_member_get_iter(new_node, i);
                    if (iter) {                        
                        data->dim_iter_map[isl_set_dim(new_prefix_sched_domain, isl_dim_set) + i] = iter;
                    }
                }
                isl_set_free(new_prefix_sched_domain);
            }
            new_node = isl_schedule_node_parent(new_node);        
        }
        isl_schedule_node_free(new_node);
    }
    isl_union_set_free(domain);
    isl_union_set_free(ref_domain);  

    return node;
}

isl_stat extract_dim_expr(__isl_take isl_basic_map *bmap, void *user) 
{
    struct build_dim_iter_map_data *data = (struct build_dim_iter_map_data *)user;    
    isl_mat *cst_mat = isl_basic_map_equalities_matrix(
        bmap, isl_dim_in, isl_dim_param, isl_dim_cst, isl_dim_div, isl_dim_out
    );        
    assert(isl_basic_map_dim(bmap, isl_dim_param) == 0);
    assert(isl_basic_map_dim(bmap, isl_dim_div) == 0);
    for (int r = 0; r < isl_mat_rows(cst_mat); r++) {
        isl_val *val = isl_mat_get_element_val(cst_mat, r, 
            isl_basic_map_dim(bmap, isl_dim_in) + isl_basic_map_dim(bmap, isl_dim_param)
            + isl_basic_map_dim(bmap, isl_dim_cst) + isl_basic_map_dim(bmap, isl_dim_div));
        int val_i = isl_val_get_num_si(val);
        isl_val_free(val);
        if (val_i != 1) {
            continue;
        }
        for (int i = 0; i < isl_basic_map_dim(bmap, isl_dim_in); i++) {
            isl_val *val = isl_mat_get_element_val(cst_mat, r, i);
            int val_i = isl_val_get_num_si(val);        
            if (val_i != 0) {
                auto it = data->dim_iter_map.find(i);
                if (it != data->dim_iter_map.end()) {
                    TPIterator *iter = data->dim_iter_map[i];                    
                    TPExpr *expr = new TPExpr(
                        "mul", 
                        new TPExpr("literal", new TPConst(val_i * (-1))), 
                        new TPExpr("literal", new TPParameter(iter->name))
                    );                    
                    data->dim_expr = data->dim_expr->add(expr);                    
                }
            }

            isl_val_free(val);
        }
        for (int i = 0; i < isl_basic_map_dim(bmap, isl_dim_cst); i++) {            
            isl_val *val = isl_mat_get_element_val(cst_mat, r, isl_basic_map_dim(bmap, isl_dim_in) + i);
            int val_i = isl_val_get_num_si(val);
            if (val_i != 0) 
                data->dim_expr = data->dim_expr->add(new TPExpr("literal", new TPConst(val_i * (-1))));            
            isl_val_free(val);
        }
    }

    isl_mat_free(cst_mat);
    isl_basic_map_free(bmap);

    return isl_stat_ok;
}

std::shared_ptr<TPArrayRef> TuningProgram::build_array_ref(
    std::string name, __isl_keep isl_map *ref, __isl_keep isl_schedule *schedule)
{
    // Step 1: Build the mapping between the sched dims to the loop iterators
    // i0 -> c0
    // i1 -> c1
    // i2 -> c2    
    struct build_dim_iter_map_data data;
    data.ref = ref;
    data.done = 0;
    isl_schedule_node *root = isl_schedule_get_root(schedule);
    root = isl_schedule_node_map_descendant_bottom_up(root, &build_dim_iter_map, &data);    
    isl_schedule_node_free(root);
    
    // Step 2: Parse the access map to build the array reference
    // [i0, i1, i2, 1] -> A[i0, i2];
    // class array_ref
    // {
    //   std::string name; // A
    //   std::vector<TPExpr *> index; // [i0, i2]
    // }
    auto tp_ref = std::make_shared<TPArrayRef>();
    tp_ref->name = name;
    int dim = isl_map_dim(ref, isl_dim_out);
    for (int i = 0; i < dim; i++) {
        // Project all the other output dims
        isl_map *ref_dim = isl_map_project_out(isl_map_copy(data.new_ref), isl_dim_out, 0, i);
        ref_dim = isl_map_project_out(ref_dim, isl_dim_out, 1, dim - i - 1);
        TPExpr *dim_expr = new TPExpr();
        data.dim_expr = dim_expr;
        isl_map_foreach_basic_map(ref_dim, &extract_dim_expr, &data);
        isl_map_free(ref_dim);
        tp_ref->index.push_back(data.dim_expr);        
    }
    isl_map_free(data.new_ref);        

    return tp_ref;
}

/* Update the array indices after tiling. 
 * Find the original parameter with the name as "tile_iter", replace it with a new expression
 * tile_iter * tile_factor + point_iter
 */
void TuningProgram::update_tiled_arrays(TPIterator *tile_iter, TPIterator *point_iter, TPParameter *tile_factor)
{    
    for (int i = 0; i < this->arrays.size(); i++) {
        TPArray *arr = this->arrays[i];
        for (int j = 0; j < arr->refs.size(); j++) {
            TPArrayRef *ref = arr->refs[j].get();     
            for (int n = 0; n < ref->index.size(); n++) {
                TPExpr *old_expr = new TPExpr("literal", new TPParameter(tile_iter->name));
                TPExpr *new_expr = new TPExpr("literal", new TPParameter(tile_iter->name));
                new_expr = (new_expr->mul(new TPExpr("literal", new TPParameter(tile_factor))))
                            ->add(new TPExpr("literal", new TPParameter(point_iter->name)));
                ref->index[n] = ref->index[n]->replace(old_expr, new_expr);
                delete old_expr;
                delete new_expr;
            }            
        }
    }    
}

std::vector<TPExpr *> TuningProgram::infer_tiled_array_bound_at_dim(int dim, std::vector<std::shared_ptr<TPArrayRef>> refs, std::vector<TPIterator *> fixed_iters)
{
    TPExpr *lb = new TPExpr();
    TPExpr *ub = new TPExpr();
    std::unordered_map<std::string, TPExpr *> iter_ubs;
    for (auto iter : this->iters) {        
        iter_ubs[iter->name] = iter->ub;
    }
    std::unordered_map<std::string, TPExpr *> iter_lbs;
    for (auto iter : this->iters) {        
        iter_lbs[iter->name] = iter->lb;
    }
    std::unordered_set<std::string> ignore_iters;
    for (auto iter : fixed_iters) {        
        ignore_iters.insert(iter->name);
    }
    for (auto ref : refs) {
        TPExpr *index = ref->index[dim];        
        TPExpr *local_lb = index->infer_bound(iter_lbs, iter_ubs, ignore_iters, 0);        
        TPExpr *local_ub = index->infer_bound(iter_lbs, iter_ubs, ignore_iters, 1);
        lb = lb->min(local_lb);
        ub = ub->max(local_ub);
    }    
    TPExpr *size = (ub->subtract(lb->dup()))->add(new TPExpr("literal", new TPConst(1)));    
    size = size->simplify();
    std::vector<TPExpr *> ret = {lb, size};

    return ret;
}

/* Given the fixed iters, infer the maximal bounds of the tiled array given the refs.
 * Construct a array tile object and return it.
 */
TPArrayTile *TuningProgram::infer_tiled_array_bounds(TPArrayTile *tile, std::vector<std::shared_ptr<TPArrayRef>> refs, std::vector<TPIterator *> fixed_iters)
{        
    std::vector<TPExpr *> lbs;
    std::vector<TPExpr *> sizes;
    int dim = refs[0]->index.size();
    for (int i = 0; i < dim; i++) {
        std::vector<TPExpr *> ret = this->infer_tiled_array_bound_at_dim(i, refs, fixed_iters);
        lbs.push_back(ret[0]);
        sizes.push_back(ret[1]);        
    }    

    tile->lbs = lbs;
    tile->sizes = sizes;

    return tile;
}

std::shared_ptr<TPExpr> TPArrayTile::compute_size() {
    TPExpr *size = new TPExpr();
    for (auto s : this->sizes) {
        size = size->mul(s->dup());
    }
    return std::shared_ptr<TPExpr>(size);
}

std::shared_ptr<TPExpr> TPIterator::compute_size() {
    TPExpr *size = this->ub->dup();
    size = size->subtract(this->lb->dup());    
    return std::shared_ptr<TPExpr>(size);
}

struct mul_space_dim_data {    
    TPExpr *num;
    int after_for;
};

isl_bool mul_space_dim(__isl_keep isl_ast_node *node, void *user) {
    struct mul_space_dim_data *data = (struct mul_space_dim_data *)user;
    if (isl_ast_node_get_type(node) == isl_ast_node_for) {
        data->after_for = 1;        
    } else if (isl_ast_node_get_type(node) == isl_ast_node_mark) {
        isl_id *id = isl_ast_node_mark_get_id(node);
        if (!strcmp(isl_id_get_name(id), "iter_info") and data->after_for) {
            TPIterator *iter = (TPIterator *)isl_id_get_user(id);                        
            if (iter && iter->space_time == "space") {
                data->num = data->num->mul(iter->compute_size().get()->dup());
            }
        }
        isl_id_free(id);
        data->after_for = 0;
    } else {
        data->after_for = 0;
    }
    return isl_bool_true;
}

std::shared_ptr<TPExpr> TuningProgram::extract_module_num(isl_ast_node *tree)
{
    TPExpr *num = new TPExpr("literal", new TPConst(1));
    struct mul_space_dim_data data;
    data.num = num;    
    data.after_for = 0;
    isl_ast_node_foreach_descendant_top_down(tree, &mul_space_dim, &data);
    return std::shared_ptr<TPExpr>(data.num);
}

struct extract_space_dim_data {    
    std::vector<std::shared_ptr<TPExpr>> dims;
    int after_for;
    int after_array;
    int io_level;
};

isl_bool extract_space_dim(__isl_keep isl_ast_node *node, void *user) {
    struct extract_space_dim_data *data = (struct extract_space_dim_data *)user;
    if (isl_ast_node_get_type(node) == isl_ast_node_for) {
        data->after_for = 1;
    } else if (isl_ast_node_get_type(node) == isl_ast_node_mark) {
        isl_id *id = isl_ast_node_mark_get_id(node);
        if (!strcmp(isl_id_get_name(id), "iter_info") and data->after_for) {
            TPIterator *iter = (TPIterator *)isl_id_get_user(id);                        
            if (iter && iter->space_time == "space") {
                data->dims.push_back(std::shared_ptr<TPExpr>(iter->compute_size().get()->dup()));                
            }
        }
        isl_id_free(id);
        data->after_for = 0;
    } else {
        data->after_for = 0;
    }
    return isl_bool_true;
}

std::vector<std::shared_ptr<TPExpr>> TuningProgram::extract_module_dims(isl_ast_node *tree)
{
    struct extract_space_dim_data data;
    data.after_for = 0;
    isl_ast_node_foreach_descendant_top_down(tree, &extract_space_dim, &data);
    return data.dims;
}

isl_bool extract_space_dim_io(__isl_keep isl_ast_node *node, void *user) {
    /* Stop at the io_mark "io_level" */
    struct extract_space_dim_data *data = (struct extract_space_dim_data *)user;    
    if (isl_ast_node_get_type(node) == isl_ast_node_mark) {
        isl_id *id = isl_ast_node_mark_get_id(node);
        if (!strcmp(isl_id_get_name(id), "iter_info")) {            
            TPIterator *iter = (TPIterator *)isl_id_get_user(id);                        
            if (iter && (data->after_array || iter->space_time == "space")) {                                
                data->dims.push_back(std::shared_ptr<TPExpr>(iter->compute_size().get()->dup()));                
            }
        }
        char io_mark[20];
        sprintf(io_mark, "io_L%d", data->io_level);        
        if (!strcmp(isl_id_get_name(id), io_mark)) {
            isl_id_free(id);                          
            return isl_bool_false;
        }        
        if (!strcmp(isl_id_get_name(id), "array")) {
            data->after_array = 1;
        }        
        isl_id_free(id);
    }    
    return isl_bool_true;
}

std::vector<std::shared_ptr<TPExpr>> TuningProgram::extract_module_dims_io(isl_ast_node *tree, int io_level)
{    
    struct extract_space_dim_data data;    
    data.after_for = 0;
    data.after_array = 0;
    data.io_level = io_level;    
    isl_ast_node_foreach_descendant_top_down(tree, &extract_space_dim_io, &data);
    return data.dims;
}

void TuningProgram::extract_module_memory_info(std::string name, int double_buffer, TPArrayTile *tile, 
    std::vector<isl_ast_node *> &asts)
{
    auto j_memory = std::make_shared<json>();
    // Extract number of modules, double buffer, ele_type, ele_size, buffer_size, data_pack_factor
    (*j_memory)["double_buffer"] = double_buffer;
    (*j_memory)["array"] = tile->name;
    (*j_memory)["ele_type"] = tile->type;
    (*j_memory)["ele_size"] = tile->ele_size;    
    (*j_memory)["buf_size"] = tile->compute_size()->to_str();
    if (tile->data_pack_factor_inter)
        (*j_memory)["data_pack_factor_inter"] = tile->data_pack_factor_inter->to_str();
    if (tile->data_pack_factor_intra)
        (*j_memory)["data_pack_factor_intra"] = tile->data_pack_factor_intra->to_str();
    TPExpr *num = new TPExpr("literal", new TPConst(1));
    for (isl_ast_node *ast : asts) {
        num = num->mul(this->extract_module_num(ast).get()->dup());
    }
    (*j_memory)["num"] = num->to_str();
    delete num;
    this->module_memory_info[name] = j_memory;
}

void TuningProgram::extract_module_compute_info(std::string name, std::string arr_type, isl_ast_node *tree)
{
    auto j_compute = std::make_shared<json>();
    // Extract number of modules, unroll factor, array type
    for (auto p : this->params) {
        if (p->attr == "SIMD_tiling_factor")
            (*j_compute)["unroll_factor"] = p->name;
    }
    (*j_compute)["ele_type"] = arr_type;
    std::shared_ptr<TPExpr> num = this->extract_module_num(tree);    
    (*j_compute)["num"] = num->to_str();
    std::vector<std::shared_ptr<TPExpr>> dims = this->extract_module_dims(tree);
    for (auto dim : dims)
        (*j_compute)["dims"].push_back(dim->to_str());
    
    this->module_compute_info[name] = j_compute;
}

void TuningProgram::extract_module_io_info(std::string name, int io_level, std::vector<isl_ast_node *> &asts)
{
    auto j_io = std::make_shared<json>();
    // Extract dims of io modules
    for (isl_ast_node *ast : asts) {
        std::vector<std::shared_ptr<TPExpr>> dims = this->extract_module_dims_io(ast, io_level);
        for (auto dim : dims)
            (*j_io)["dims"].push_back(dim->to_str());
    }
    if ((*j_io)["dims"].size() == 0) {
        TPExpr *num = new TPExpr("literal", new TPConst(1));
        (*j_io)["dims"].push_back(num->to_str());
        delete num;
    }


    this->module_io_info[name] = j_io;
}