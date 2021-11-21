import argparse
from datetime import datetime
import logging
import numpy as np
import os
import pickle
import concurrent.futures
import json
import pprint

import utils
from tuners import Constraint
from design import Design
from explorer import ArchExplorer

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('--outdir', type=str, default="outdir", help="output directory")
    parser.add_argument('--db', type=str, default="db", help="search database")
    parser.add_argument('--use-db', type=int, default=1, help="use database")
    parser.add_argument('--objective', type=str, default="latency", help="optimization target [latency, off_chip_comm, energy, dsp_num]")
    parser.add_argument('--cst', type=str, default="hw_cst", help="hardware constraint")
    parser.add_argument('--stop-after-epochs', type=int, default=-1, help="number of epochs of the unit searching task")
    parser.add_argument('--stop-after-time', type=int, default=-1, help="number of epochs of the unit searching task")
    parser.add_argument('--n-worker', type=int, default=8, help="number of workers for multi-processing")
    parser.add_argument('--designs', type=str, default="designs", help="systolic array design directory")
    parser.add_argument('--design-idx', type=int, default=-1, help="systolic array design index")
    parser.add_argument('--workload', type=str, required=True, help="searching workload")
    # Architecture specific options
    parser.add_argument('--explore-fusion', action="store_true", help="explore layer fusion in a single accelerator")
    parser.add_argument('--explore-multi-acc', action="store_true", help="explore using multiple accelerators")
    parser.add_argument('--explore-programmable', action="store_true", help="explore programmable systolic array")
    parser.add_argument('--multi-array-mode', type=int, default=0, help="execution mode of the generic array in the multi-acc setting")
    parser.add_argument('--use-uram', type=int, default=0, help="use URAM for the intermediate data in the fused array")
    parser.add_argument('--use-uram-all', action="store_true", help="use URAM for all the arrays in the multi-array system")
    parser.add_argument('--method', type=str, default="customized1", help="searching method")
    parser.add_argument('--unit-task-method', type=str, default="genetic", help="unit task searching method")
    #parser.add_argument('--multi-batch', action="store_true", help="use multiple batches in the multi-acc array")
    parser.add_argument('--batch-size', type=int, default=1, help="use multiple batches in the multi-acc array")
    parser.add_argument('--profiling', action="store_true", help="profiling")
    parser.add_argument('--max-n-array', type=int, default=8, help="maximal number of arrays")
    # Algorithm specific options
    parser.add_argument('--xgb-n-gens', type=int, default=5)
    parser.add_argument('--xgb-thres', type=float, default=0.6)
    parser.add_argument('--xgb-thres-adjust', type=float, default=0.4)

    args = parser.parse_args()

    search_obj = args.objective

    # Set up the working directory
    now = datetime.now()
    outdir = args.outdir
    os.makedirs(outdir, exist_ok=True)
    explore_config = ""
    explore_config += "f1" if args.explore_fusion else "f0"
    explore_config += "ma1" if args.explore_multi_acc else "ma0"
    explore_config += "p1" if args.explore_programmable else "p0"
    explore_config += f"mam{args.multi_array_mode}"
    explore_config += f"u{args.use_uram}"
    exp_name = f"O_{args.objective}-W_{args.workload}-C_{explore_config}-T_{now.date()}-{now.time()}"
    outdir = f"{outdir}/{exp_name}"
    os.makedirs(outdir, exist_ok=True)
    logger = utils.init_logger(outdir)

    # Load the hardware constraints
    cst = Constraint(f'cst/{args.cst}.json')

    # Load the workloads
    with open(f'workload/{args.workload}.json') as f:
        data = json.load(f)
    workloads = []
    for workload in data['workloads']:
        workloads.append(workload)

    # Load the designs
    design_dir = args.designs
    os.makedirs(f"{design_dir}/register", exist_ok=True)
    designs = []
    for f in os.listdir(design_dir):
        if f.endswith(".json"):
            with open(f'{design_dir}/{f}', 'r') as json_f:
                desp = json.load(json_f)
            design = Design(f.split(".")[0])
            design.register(desp, f"{design_dir}/register/{design.name}.py")
            designs.append(design)
    def get_design_name(elem):
        return elem.name
    # Sort the designs by names
    designs.sort(key=get_design_name)
    if len(designs) == 0:
        raise RuntimeError("No systolic array design was found.")
    #for design in designs:
    #    print(design.name)

    # Update the search stop criteria
    max_epochs = -1
    max_time = -1
    if args.stop_after_epochs > 0:
        max_epochs = args.stop_after_epochs
    elif args.stop_after_time > 0:
        max_time = args.stop_after_time
    else:
        max_time = 60 # 60 seconds by default

    # Load the search database if existed
    db_file = f'{args.db}/{str(cst)}.db'
    if os.path.exists(db_file) and args.use_db:
        search_db = pickle.load(open(db_file, 'rb'))
        logger.info('Found existing tuning database!')
    else:
        search_db = None

    # Start search
    counter = utils.PerfCounter(logger)
    counter.init_counter("total_search_time")

    search_config = {
        "method": args.method, # [customized1, customized2, exhaustive]
        "n_worker": args.n_worker,
        "unit_task_method": args.unit_task_method, # [exhaustive_pruning, random, sa, bayesian, opentuner, RL]
        "profiling": args.profiling,
        "workload": args.workload,
        "design_idx": args.design_idx,
        "genetic_params": {"population_size": [200, 20]},
        "args": args,
        "search_records_db": {} if search_db == None else search_db,
        "explore_fusion": args.explore_fusion,
        "explore_multi_acc": args.explore_multi_acc,
        "explore_programmable": args.explore_programmable,
        "multi_array_mode": args.multi_array_mode,        
        "use_db": args.use_db,
        "use_uram": args.use_uram,
        "use_uram_all": args.use_uram_all,
        "batch_size": args.batch_size,
        "max_n_array": args.max_n_array,
        "xgb_params": {
            "n_gens": args.xgb_n_gens,
            "thres": args.xgb_thres,
            "thres_adjust": args.xgb_thres_adjust
        }
    }    

    explorer = ArchExplorer(cst, search_obj, max_epochs, max_time, search_config, designs, workloads)
    search_record = explorer.search()

    # Update the database
    search_db = explorer.search_config["search_records_db"]
    if os.path.exists(db_file):
        old_search_db = pickle.load(open(db_file, 'rb'))
        for search_task in search_db:
            if search_task in old_search_db:
                old_search_db[search_task].update(search_db[search_task])
            else:
                old_search_db[search_task] = search_db[search_task]
        pickle.dump(old_search_db, open(db_file, 'wb'))
    else:
        pickle.dump(search_db, open(db_file, 'wb'))

    counter.update_counter("total_search_time")
    counter.print_counter("total_search_time")

    # Display and dump out the search results
    #def print_records(record, num):
    #    num += 1
    #    if num > 10:
    #        return
    #    while record.records:
    #        print(record.task_names, len(record.records))
    #        for r in record.records:
    #            print_records(r, num)
    #print_records(search_record, 0)

    logger.info(f'{search_record.to_str()}')
    with open(f'{outdir}/history.log', 'w') as f:
        f.write(search_record.to_str())
