import argparse
from datetime import datetime
import logging
import numpy as np
import os
import pickle
import concurrent.futures
import json
import pprint

from design import Design
from constraint import Constraint
from search_task import SearchTask
import utils
import tuner

if __name__ == "__main__":
    cst = Constraint(f'cst/hw_cst.json')
    max_epochs = -1
    max_time = 20
    search_obj = "latency"

    # Set up the working directory
    now = datetime.now()
    outdir = "outdir"
    os.makedirs(outdir, exist_ok=True)    
    explore_config = ""
    exp_name = f"O_{search_obj}-C_{explore_config}-T_{now.date()}-{now.time()}"
    outdir = f"{outdir}/{exp_name}"
    os.makedirs(outdir, exist_ok=True)
    logger = utils.init_logger(outdir)

    design_dir = "/curr/jaywang/research/autosa/AutoSA/autosa.tmp/output/tuning"
    designs = []
    for f in os.listdir(design_dir):
        if f.endswith(".json"):
            with open(f'{design_dir}/{f}', 'r') as json_f:
                desp = json.load(json_f)
            design = Design(f.split(".")[0])
            design.register(desp, f"{design_dir}/register/{design.name}.py")
            designs.append(design)
    if len(designs) == 0:
        raise RuntimeError("No design found")

    # Load task    
    with open(f'task/mm.json') as f:
        data = json.load(f)
    tasks = []
    for task in data["tasks"]:
        tasks.append(task)

    # Start searching
    for task in tasks:
        search_record = utils.SearchRecord().reset()
        for design in designs:
            search_task = SearchTask(design , task)
            task_params = {
                "p0": 1024, "p1": 1024, "p2": 1024,
                "p3": 350, "p4": 266, "p5": 64,
                "p6": 38, "p7": 10, "p8": 8
            }
            reward, resource = search_task.evaluate(task_params)
            print(1/reward)
            print(resource)
            #search_record.update(tuner.genetic_search(search_task, cst, search_obj, logger, max_epochs, max_time))
        #task["search results"] = search_record

    #for task in tasks:
    #    logger.info(pprint.pformat(task, indent=4))