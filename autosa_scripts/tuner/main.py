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
    parser = argparse.ArgumentParser()
    parser.add_argument('--outdir', type=str, default="outdir", help="output directory")
    parser.add_argument('--db', type=str, default="db", help="search database")
    parser.add_argument('--objective', type=str, default="latency", help="optimization target")
    parser.add_argument('--cst', type=str, default="hw_cst", help="hardware constraint")
    parser.add_argument('--stop-after-epochs', type=int, default=-1, help="number of epochs of the unit searching task")
    parser.add_argument('--stop-after-time', type=int, default=-1, help="number of epochs of the unit searching task")
    parser.add_argument('--use-db', type=int, default=1, help="use database")
    parser.add_argument('--n-thread', type=int, default=16, help="number of threads to use for searching")
    parser.add_argument('--designs', type=str, default="designs", help="systolic array design directory")
    parser.add_argument('--task', type=str, default="mm", help="search task")

    args = parser.parse_args()
    
    search_obj = args.objective    
    
    # Set up the working directory
    now = datetime.now()
    outdir = args.outdir
    os.makedirs(outdir, exist_ok=True)    
    explore_config = ""
    exp_name = f"O_{args.objective}-C_{explore_config}-T_{now.date()}-{now.time()}"
    outdir = f"{outdir}/{exp_name}"
    os.makedirs(outdir, exist_ok=True)
    logger = utils.init_logger(outdir)

    # Load the constraints
    cst = Constraint(f'cst/{args.cst}.json')

    # Set up the searching algorithm stop criteria
    max_epochs = -1
    max_time = -1
    if args.stop_after_epochs > 0:
        max_epochs = args.stop_after_epochs
    elif args.stop_after_time > 0:
        max_time = args.stop_after_time
    else:
        max_time = 60

    # Set up the parallel executor    
    # TODO

    # Register designs    
    design_dir = args.designs
    os.makedirs(f"{design_dir}/register", exist_ok=True)
    designs = []
    for f in os.listdir(design_dir):
        if f.endswith(".json"):
            with open(f'{design_dir}/{f}', 'r') as json_f:
                desp = json.load(json_f)
            design = Design(f.split(".")[0])
            design.register(desp, f"{design_dir}/register/{design.name}.py")
            #print(design.name)
            designs.append(design)
    if len(designs) == 0:
        raise RuntimeError("No design found")        
    #exit(0)

    # Load task
    with open(f'task/{args.task}.json') as f:
        data = json.load(f)
    tasks = []
    for task in data["tasks"]:
        tasks.append(task)

    # Start searching
    counter = utils.PerfCounter(logger)
    counter.init_counter("Total Search Time")
    all_records = []        
    for task in tasks:
        search_record = utils.SearchRecord().reset()
        #for design in [designs[4]]:
        for design in designs:
            search_task = SearchTask(design, task)
            record = tuner.genetic_search(search_task, cst, search_obj, logger, max_epochs, max_time)
            all_records.append(record)
            search_record.update(record)
        task["search results"] = search_record

    counter.update_counter("Total Search Time")
    counter.print_counter("Total Search Time")

    print(all_records)

    # Display and dump the search history
    #for task in tasks:
    #    logger.info(pprint.pformat(task, indent=4))
    with open(f"{outdir}/results.log", 'w') as f:
        f.write(pprint.pformat(task, indent=4))
    with open(f"{outdir}/history.log", 'w') as f:
        f.write(pprint.pformat(all_records, indent=4))