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

TPExpr *TPExpr::div_by_param(TPExpr *divisor) {        
    TPExpr *expr = new TPExpr("div", this, divisor);
    return expr;
}

TPExpr *TPExpr::ceil() {    
    TPExpr *expr = new TPExpr("ceil", this);
    return expr;
}

TPExpr *TPExpr::add(TPExpr *expr) {
    if (this->func == "NULL") {        
        delete this;
        return expr;        
    } else {
        TPExpr *new_expr = new TPExpr("add", this, expr);
        return new_expr;
    }
}

TPExpr *TPExpr::mul(TPExpr *expr) {    
    TPExpr *new_expr = new TPExpr("mul", this, expr);
    return new_expr;    
}

TPExpr *TPExpr::subtract(TPExpr *expr) {    
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

TPExpr *TPExpr::min(TPExpr *expr) {    
    if (this->func == "literal" && dynamic_cast<TPConst *>(this->ops[0])) {        
        int val = ((TPConst *)(this->ops[0]))->val;
        if (expr->func == "literal" && dynamic_cast<TPConst *>(expr->ops[0])) {
            val = std::min(val, ((TPConst *)(expr->ops[0]))->val);
            return new TPExpr("literal", new TPConst(val));
        }
    } else if (this->func == "NULL") {
        delete this;
        return expr->dup();
    }
    TPExpr *new_expr = new TPExpr("min", this, expr);
    return new_expr;
}

TPExpr *TPExpr::max(TPExpr *expr) {
    if (this->func == "literal" && dynamic_cast<TPConst *>(this->ops[0])) {        
        int val = ((TPConst *)(this->ops[0]))->val;
        if (expr->func == "literal" && dynamic_cast<TPConst *>(expr->ops[0])) {
            val = std::max(val, ((TPConst *)(expr->ops[0]))->val);
            return new TPExpr("literal", new TPConst(val));
        }
    } else if (this->func == "NULL") {
        delete this;
        return expr->dup();
    }
    TPExpr *new_expr = new TPExpr("max", this, expr);
    return new_expr;
}

/* Create a duplicate of the current expression. */
TPExpr *TPExpr::dup() {
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

TPParameter *TPParameter::dup() {
    TPParameter *new_param = new TPParameter();
    new_param->name = this->name;
    new_param->type = this->type;
    for (auto bound : this->bounds) {
        new_param->bounds.push_back(bound->dup());
    }
    new_param->div = this->div;
    new_param->dep_param = this->dep_param;
    new_param->dep_iter = this->dep_iter;
    new_param->tune = this->tune;
    new_param->attr = this->attr; 

    return new_param;
}

TPConst *TPConst::dup() {
    TPConst *new_const = new TPConst();
    new_const->type = this->type;
    new_const->val = this->val;

    return new_const; 
}

/* Replace the expression that matches "match" with replace.
 */
TPExpr *TPExpr::replace(TPExpr *match, TPExpr *replace) {        
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
            std::cout << "[AutoSA] Error: unsupported TPExpr function type: " << this->func << std::endl;
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
    }
    else {
        std::cout << "[AutoSA] Error: unsupported TPExpr function type: " << this->func << std::endl;
        exit(1);
    }
}

TPExpr *TPExpr::infer_bound(
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
                    return ubs[param->name]->dup();
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
        std::cout << "[AutoSA] Error: unsupported TPExpr function type: " << this->func << std::endl;
        exit(1);
    } else if (this->func == "ceil") {
        std::cout << "[AutoSA] Error: unsupported TPExpr function type: " << this->func << std::endl;
        exit(1);
    } else if (this->func == "div") {
        std::cout << "[AutoSA] Error: unsupported TPExpr function type: " << this->func << std::endl;
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
        std::cout << "[AutoSA] Error: unsupported TPExpr function type: " << this->func << std::endl;
        exit(1);
    }
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
            //TPParameter *lb = new TPParameter("p" + std::to_string(prog->params.size()));
            //prog->params.push_back(lb);
            //prog->param_map[lb->name] = lb;
            //lb->tune = false;          
            //lb->attr = "loop_lb";
            TPParameter *ub = new TPParameter("p" + std::to_string(prog->params.size()));
            prog->params.push_back(ub);
            prog->param_map[ub->name] = ub;
            ub->tune = false;
            ub->attr = "loop_ub";

            TPIterator *iter = new TPIterator(
                "c" + std::to_string(prog->iters.size()),                
                new TPExpr("literal", new TPConst(0)),
                new TPExpr("literal", new TPParameter(ub)));
            //lb->dep_iter = iter;
            ub->dep_iter = iter;                
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

/* Update the band iters after tiling. The "node" points to the tile band. 
 */
__isl_give isl_schedule_node *TuningProgram::tile(__isl_take isl_schedule_node *node, int div)
{
    isl_schedule_node *tile_node = node;
    isl_schedule_node *point_node = isl_schedule_node_child(isl_schedule_node_copy(node), 0);
    int n = isl_schedule_node_band_n_member(point_node);
    for (int i = 0; i < n; i++) {                
        /* We assume all the loops start from zero for now. */
        TPIterator *tile_iter = (TPIterator *)isl_schedule_node_band_member_get_iter(tile_node, i);
        TPParameter *point_ub = new TPParameter("p" + std::to_string(this->params.size()));
        point_ub->tune = true;
        point_ub->div = div;
        point_ub->bounds.push_back(new TPExpr("literal", new TPConst(1)));
        TPParameter *tile_ub = (TPParameter *)(tile_iter->ub->ops[0]);
        point_ub->bounds.push_back(new TPExpr("literal", new TPParameter(tile_ub)));
        point_ub->dep_param = this->param_map[tile_ub->name];
        point_ub->attr = "tile_factor";
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
        if (isl_schedule_node_band_member_get_space_time(tile_node, i) == autosa_loop_space)
            point_iter->space_time = "space";
        else
            point_iter->space_time = "time";
        point_ub->dep_iter = point_iter;            
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
__isl_give isl_schedule_node *TuningProgram::tile(__isl_take isl_schedule_node *node, int pos, int div)
{
    isl_schedule_node *tile_node = node;
    isl_schedule_node *point_node = isl_schedule_node_child(isl_schedule_node_copy(node), 0);    
    TPIterator *tile_iter = (TPIterator *)isl_schedule_node_band_member_get_iter(tile_node, pos);
    TPParameter *point_ub = new TPParameter("p" + std::to_string(this->params.size()));
    point_ub->tune = true;
    point_ub->div = div;
    point_ub->bounds.push_back(new TPExpr("literal", new TPConst(1)));
    TPParameter *tile_ub = (TPParameter *)(tile_iter->ub->ops[0]);
    point_ub->bounds.push_back(new TPExpr("literal", new TPParameter(tile_ub)));
    point_ub->dep_param = this->param_map[tile_ub->name];
    point_ub->attr = "tile_factor";
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
    point_ub->dep_iter = point_iter;
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
        j_param["divisable"] = param->div;
        if (param->dep_param) {
            j_param["dep_param"] = param->dep_param->name;
        }
        if (param->dep_iter) {
            j_param["dep_iter"] = param->dep_iter->name;
        }
        j_param["tunable"] = param->tune;
        j_param["attr"] = param->attr;    
        if (param->bounds.size() > 0)
            j_param["bounds"] = {param->bounds[0]->to_str(), param->bounds[1]->to_str()};
        j_params.push_back(j_param);
    }
    j["params"] = j_params;

    // loop struct - latency
    json j_module_latency;
    for (auto x: this->module_loop_info) {        
        j["modules"][x.first]["latency"] = *x.second;
    }
    
    // design stats - resource

    std::ofstream o(dir + "/kernel" + std::to_string(this->id) + ".json");
    o << std::setw(4) << j << std::endl;
    o.close();

    // Debug
    //for (int i = 0; i < this->arrays.size(); i++) {
    //    TPArray *arr = this->arrays[i];
    //    for (int j = 0; j < arr->refs.size(); j++) {
    //        TPArrayRef *ref = arr->refs[j];
    //        std::cout << ref->to_str() << std::endl;            
    //    }
    //}

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
            if (iter) {
                isl_id *id = isl_id_alloc(ctx, "iter_info", iter);
                node = isl_schedule_node_insert_mark(node, id);
            }
            if (i > 0) {
                node = isl_schedule_node_parent(node);
            }
        }
        node = isl_schedule_node_parent(node);
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

std::shared_ptr<json> extract_loop_info(__isl_keep isl_ast_node *node, void *user)
{
    //json j_info;
    std::shared_ptr<json> j_info;
    enum isl_ast_node_type type;    
    isl_ctx *ctx = isl_ast_node_get_ctx(node);
    type = isl_ast_node_get_type(node);

    switch(type) {
        case isl_ast_node_for:
        {            
            TPIterator *iter = (TPIterator *)user;            
            isl_ast_node *child;
            child = isl_ast_node_for_get_body(node);
            std::shared_ptr<json> j_child = extract_loop_info(child, NULL);
            isl_ast_node_free(child);
            if (iter) {                
                j_info = std::make_shared<json>();
                *j_info = {{"type", "for"}, {"iterator", iter->name}};
                (*j_info)["bounds"].push_back(iter->lb->to_str());                
                (*j_info)["bounds"].push_back(iter->ub->to_str());
                (*j_info)["child"] = *j_child;
            } else {
                j_info = j_child;
            }            

            break;
        }
        case isl_ast_node_block:
        {
            /* Extract the block information and insert it into the loop struc. */
            j_info = std::make_shared<json>();
            *j_info = {{"type", "block"}, {"child", {}}};
            isl_ast_node_list *child_list = isl_ast_node_block_get_children(node);
            int n_child = isl_ast_node_list_n_ast_node(child_list);
            for (int i = 0; i < n_child; i++) {
                isl_ast_node *child = isl_ast_node_list_get_ast_node(child_list, i);
                std::shared_ptr<json> j_child = extract_loop_info(child, NULL);
                isl_ast_node_free(child);
                (*j_info)["child"].push_back(*j_child);
            }
            isl_ast_node_list_free(child_list);

            break;
        }
        case isl_ast_node_user:
        {
            /* Print nothing. */
            j_info = std::make_shared<json>();
            std::shared_ptr<json> j_user = extract_isl_ast_node_user(node);
            *j_info = {{"type", "user"}, {"child", *j_user}};

            break;
        }
        case isl_ast_node_if: 
        {
            j_info = std::make_shared<json>();
            *j_info = {{"type", "if"}, {"child", {}}};
            isl_ast_node *then_child, *else_child;
            then_child = isl_ast_node_if_get_then_node(node);
            std::shared_ptr<json> j_then = extract_loop_info(then_child, NULL);
            isl_ast_node_free(then_child);
            (*j_info)["child"].push_back(*j_then);

            else_child = isl_ast_node_if_get_else_node(node);
            if (else_child) {
                std::shared_ptr<json> j_else = extract_loop_info(else_child, NULL);
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
                /* For loop */                
                iter = (TPIterator *)isl_id_get_user(id);
                //std::cout << iter->name << std::endl;
                isl_ast_node *child = isl_ast_node_mark_get_node(node);
                j_info = extract_loop_info(child, iter);
                isl_ast_node_free(child);
            } else {
                std::string mark_content(isl_id_get_name(id));
                j_info = std::make_shared<json>();
                *j_info = {{"type", "mark"}, {"content", mark_content}};
                isl_ast_node *child = isl_ast_node_mark_get_node(node);
                std::shared_ptr<json> j_child = extract_loop_info(child, iter);
                isl_ast_node_free(child);                
                (*j_info)["child"] = *j_child;
            }
            isl_id_free(id);            

            break;
        }
        default:
        {
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
        j_loop = extract_loop_info(ast[0], NULL);
        this->module_loop_info[name] = j_loop;
    } else if (ast.size() == 3) {        
        // outer module
        std::shared_ptr<json> j_loop1;
        j_loop1 = extract_loop_info(ast[0], NULL);
        this->module_loop_info[name] = j_loop1;
        // intra module
        std::shared_ptr<json> j_loop2;
        j_loop2 = extract_loop_info(ast[1], NULL);        
        this->module_loop_info[name + "_intra"] = j_loop2;
        // inter module
        std::shared_ptr<json> j_loop3;
        j_loop3 = extract_loop_info(ast[2], NULL);
        this->module_loop_info[name + "_inter"] = j_loop3;
    }

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
        //DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
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
                        //std::cout << isl_set_dim(new_prefix_sched_domain, isl_dim_set) + i << std::endl;
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
    //DBGBMAP(stdout, bmap, isl_basic_map_get_ctx(bmap));
    isl_mat *cst_mat = isl_basic_map_equalities_matrix(
        bmap, isl_dim_in, isl_dim_param, isl_dim_cst, isl_dim_div, isl_dim_out
    );    
    //print_mat(stdout, cst_mat);
    //assert(isl_mat_rows(cst_mat) == 1);
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
                    //std::cout << "f0: " << val_i << ": " << iter->name << std::endl;
                    TPExpr *expr = new TPExpr(
                        "mul", 
                        new TPExpr("literal", new TPConst(val_i * (-1))), 
                        new TPExpr("literal", new TPParameter(iter->name))
                    );
                    //std::cout << "f1: " << expr->to_str() << std::endl;
                    data->dim_expr = data->dim_expr->add(expr);
                    //std::cout << "f2: " << data->dim_expr->to_str() << std::endl;
                }
            }

            isl_val_free(val);
        }
        for (int i = 0; i < isl_basic_map_dim(bmap, isl_dim_cst); i++) {
            //std::cout << "here" << std::endl;
            isl_val *val = isl_mat_get_element_val(cst_mat, r, isl_basic_map_dim(bmap, isl_dim_in) + i);
            int val_i = isl_val_get_num_si(val);
            if (val_i != 0) 
                data->dim_expr = data->dim_expr->add(new TPExpr("literal", new TPConst(val_i * (-1))));
            //std::cout << "f3: " << data->dim_expr->to_str() << std::endl;            
            isl_val_free(val);
        }
    }

    isl_mat_free(cst_mat);
    isl_basic_map_free(bmap);

    return isl_stat_ok;
}

TPArrayRef *TuningProgram::build_array_ref(
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
    TPArrayRef *tp_ref = new TPArrayRef();
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
        //std::cout << data.dim_expr->to_str() << std::endl;
    }
    isl_map_free(data.new_ref);    
    //exit(0);

    return tp_ref;
}

//void infer_bound() 
//{
//    // index: [p0*i0 + i3, i2]
//    // bound: min(i3)->max(i3), 1
//    // data_pack: param: bounds (1, 1) div by 1
//}

/* Update the array indices after tiling. 
 * Find the original parameter with the name as "tile_iter", replace it with a new expression
 * tile_iter * tile_factor + point_iter
 */
void TuningProgram::update_tiled_arrays(TPIterator *tile_iter, TPIterator *point_iter, TPParameter *tile_factor)
{    
    for (int i = 0; i < this->arrays.size(); i++) {
        TPArray *arr = this->arrays[i];
        for (int j = 0; j < arr->refs.size(); j++) {
            TPArrayRef *ref = arr->refs[j];
            //std::cout << ref->to_str() << std::endl;
            for (int n = 0; n < ref->index.size(); n++) {
                TPExpr *old_expr = new TPExpr("literal", new TPParameter(tile_iter->name));
                TPExpr *new_expr = new TPExpr("literal", new TPParameter(tile_iter->name));
                new_expr = (new_expr->mul(new TPExpr("literal", new TPParameter(tile_factor))))
                            ->add(new TPExpr("literal", new TPParameter(point_iter->name)));
                ref->index[n] = ref->index[n]->replace(old_expr, new_expr);
                delete old_expr;
                delete new_expr;
            }
            //std::cout << ref->to_str() << std::endl;
        }
    }
    //exit(0);
}

std::vector<TPExpr *> TuningProgram::infer_tiled_array_bound_at_dim(int dim, std::vector<TPArrayRef *> refs, std::vector<TPIterator *> fixed_iters)
{
    TPExpr *lb = new TPExpr();
    TPExpr *ub = new TPExpr();
    std::unordered_map<std::string, TPExpr *> iter_ubs;
    for (auto iter : this->iters) {
        //std::cout << iter->name << " ub: " << iter->ub->to_str() << std::endl;
        iter_ubs[iter->name] = iter->ub;
    }
    std::unordered_map<std::string, TPExpr *> iter_lbs;
    for (auto iter : this->iters) {
        //std::cout << iter->name << " lb: " << iter->lb->to_str() << std::endl;
        iter_lbs[iter->name] = iter->lb;
    }
    std::unordered_set<std::string> ignore_iters;
    for (auto iter : fixed_iters) {
        //std::cout << "ignore: " << iter->name << std::endl;
        ignore_iters.insert(iter->name);
    }
    for (auto ref : refs) {
        TPExpr *index = ref->index[dim];
        //std::cout << index->to_str() << std::endl;
        TPExpr *local_lb = index->infer_bound(iter_lbs, iter_ubs, ignore_iters, 0);        
        TPExpr *local_ub = index->infer_bound(iter_lbs, iter_ubs, ignore_iters, 1);
        lb->min(local_lb);
        ub->max(local_ub);        
        delete local_lb;
        delete local_ub;
    }
    //std::cout << lb->to_str() << std::endl;
    //std::cout << ub->to_str() << std::endl;                            
    TPExpr *size = ub->subtract(lb->dup());
    //std::cout << lb->to_str() << std::endl;
    //std::cout << size->to_str() << std::endl;                            
    //exit(0);
    std::vector<TPExpr *> ret = {lb, size};

    return ret;
}

/* Given the fixed iters, infer the maximal bounds of the tiled array given the refs.
 * Construct a array tile object and return it.
 */
TPArrayTile *TuningProgram::infer_tiled_array_bounds(TPArrayTile *tile, std::vector<TPArrayRef *> refs, std::vector<TPIterator *> fixed_iters)
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