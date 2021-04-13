/* This function defines all the functions used for AutoSA tuning.
 * When executed in the tuning mode, AutoSA will automatically optimize the program,
 * applying different permutation and tiling techniques.
 * The program transform history and program loop structure are recorded, which 
 * are later used by the auto-tuner.
 */
#include <iomanip>
#include <iostream>
#include <fstream>

#include "autosa_tuning.h"
#include "json.hpp"

using json = nlohmann::json;

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
    } else {
        return "";
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
            // We assume the loop bounds are independent for now.
            TPParameter *lb = new TPParameter("p" + std::to_string(prog->params.size()));
            prog->params.push_back(lb);
            prog->param_map[lb->name] = lb;
            lb->tune = false;            
            TPParameter *ub = new TPParameter("p" + std::to_string(prog->params.size()));
            prog->params.push_back(ub);
            prog->param_map[ub->name] = ub;
            ub->tune = false;

            TPIterator *iter = new TPIterator(
                "c" + std::to_string(prog->iters.size()),
                new TPExpr("literal", new TPParameter(*lb)),
                new TPExpr("literal", new TPParameter(*ub)));
            lb->dep_iter = iter;
            ub->dep_iter = iter;                
            // Assign the iterator to schedule dim                        
            node = isl_schedule_node_band_member_set_iter(node, i, (void *)iter);
            //TPIterator *new_iter = (TPIterator *)isl_schedule_node_band_member_get_iter(node, i);
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
        TPIterator *tile_iter = (TPIterator *)isl_schedule_node_band_member_get_iter(tile_node, i);
        TPParameter *point_ub = new TPParameter("p" + std::to_string(this->params.size()));
        point_ub->tune = true;
        point_ub->div = div;
        point_ub->bounds.push_back(new TPExpr("literal", new TPConst(1)));
        TPParameter *tile_ub = (TPParameter *)(tile_iter->ub->ops[0]);
        point_ub->bounds.push_back(new TPExpr("literal", new TPParameter(tile_ub)));
        point_ub->dep_param = this->param_map[tile_ub->name];
        this->params.push_back(point_ub);
        this->param_map[point_ub->name] = point_ub;
                
        // Update the loop bound
        if (div == 0)
            tile_iter->ub = tile_iter->ub->div_by_param(new TPExpr("literal", new TPParameter(*point_ub)))->ceil();
        else
            tile_iter->ub = tile_iter->ub->div_by_param(new TPExpr("literal", new TPParameter(*point_ub)));

        // Point loop                        
        TPIterator *point_iter = new TPIterator(
            "c" + std::to_string(this->iters.size()), 
            new TPExpr("literal", new TPConst(0)), 
            new TPExpr("literal", new TPParameter(*point_ub)));
        point_ub->dep_iter = point_iter;            
        point_node = isl_schedule_node_band_member_set_iter(point_node, i, (void *)point_iter);
        this->iters.push_back(point_iter);
    }

    isl_schedule_node_free(tile_node);
    node = isl_schedule_node_parent(point_node);
    //isl_schedule_node_free(point_node);

    return node;
}

/* Update the band iters after tiling. The "node" points to the tile band. 
 * Dim "pos" in the band is tiled. Point band contains a single loop.
 */
__isl_give isl_schedule_node *TuningProgram::tile(__isl_take isl_schedule_node *node, int pos, int div)
{
    isl_schedule_node *tile_node = node;
    isl_schedule_node *point_node = isl_schedule_node_child(isl_schedule_node_copy(node), 0);
    //int n = isl_schedule_node_band_n_member(point_node);
    //for (int i = 0; i < n; i++) {                
        //TPIterator *tile_iter = (TPIterator *)isl_schedule_node_band_member_get_iter(tile_node, i);
        TPIterator *tile_iter = (TPIterator *)isl_schedule_node_band_member_get_iter(tile_node, pos);
        TPParameter *point_ub = new TPParameter("p" + std::to_string(this->params.size()));
        point_ub->tune = true;
        point_ub->div = div;
        point_ub->bounds.push_back(new TPExpr("literal", new TPConst(1)));
        TPParameter *tile_ub = (TPParameter *)(tile_iter->ub->ops[0]);
        point_ub->bounds.push_back(new TPExpr("literal", new TPParameter(tile_ub)));
        point_ub->dep_param = this->param_map[tile_ub->name];
        this->params.push_back(point_ub);
        this->param_map[point_ub->name] = point_ub;
                
        // Update the loop bound
        if (div == 0)
            tile_iter->ub = tile_iter->ub->div_by_param(new TPExpr("literal", new TPParameter(*point_ub)))->ceil();
        else
            tile_iter->ub = tile_iter->ub->div_by_param(new TPExpr("literal", new TPParameter(*point_ub)));

        // Point loop                        
        TPIterator *point_iter = new TPIterator(
            "c" + std::to_string(this->iters.size()), 
            new TPExpr("literal", new TPConst(0)), 
            new TPExpr("literal", new TPParameter(*point_ub)));
        point_ub->dep_iter = point_iter;
        point_node = isl_schedule_node_band_member_set_iter(point_node, 0, (void *)point_iter);
        this->iters.push_back(point_iter);
    //}

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
        std::cout << param->name << std::endl;
        // TODO
        if (param->bounds.size() > 0)
            j_param["bounds"] = {param->bounds[0]->to_str(), param->bounds[1]->to_str()};
        j_params.push_back(j_param);
    }
    j["params"] = j_params;

    // loop struct - latency

    // design stats - resource

    std::ofstream o(dir + "/kernel" + std::to_string(this->id) + ".json");
    o << std::setw(4) << j << std::endl;
    o.close();

    return;
}