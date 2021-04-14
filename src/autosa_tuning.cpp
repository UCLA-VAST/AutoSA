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
        std::string ret = "(";
        ret += this->ops[0]->to_str();
        ret += ")/(";
        ret += this->ops[1]->to_str();
        ret += ")";
        return ret;
    } else {
        std::cout << "[AutoSA] Error: unsupported TPExpr function type!" << std::endl;
        exit(1);
    }
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