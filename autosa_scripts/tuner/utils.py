import time
import functools
import math
import logging
import itertools
from datetime import datetime
from subprocess import Popen, PIPE
import json
import pprint
import concurrent.futures
import queue

class PerfCounter(object):
    def __init__(self, logger):
        self.logger = logger
        self.counters = {}
    
    def init_counter(self, name):        
        self.counters[name] = {'start': time.perf_counter(), 'elapsed': 0}
        
    def update_counter(self, name):
        if name not in self.counters:
            raise RuntimeError(f"Counter {name} is not defined")
        now = time.perf_counter()
        self.counters[name]['elapsed'] += (now - self.counters[name]['start'])
        self.counters[name]['start'] = now

    def get_counter(self, name):
        if name not in self.counters:
            raise RuntimeError(f"Counter {name} is not defined")
        return self.counters[name]['elapsed']

    def print_counter(self, name):
        if name not in self.counters:
            raise RuntimeError(f"Counter {name} is not defined")
        self.logger.info(f'[Event: {name}] Total elapsed time: {self.counters[name]["elapsed"]:.4f} s')

    def print_counters(self):
        for name in self.counters:
            self.logger.info(f'[Event: {name}] Total elapsed time: {self.counters[name]["elapsed"]:.4f} s')

def init_logger(outdir):	
    logger = logging.getLogger('AutoSA-Tuner')
    # If there is already any handlers, remove them	
    for handler in logger.handlers[:]:
        handler.close()
        logger.removeHandler(handler)
    formatter = logging.Formatter(
                '[%(name)s %(asctime)s] %(levelname)s: %(message)s',
                '%Y-%m-%d %H:%M:%S')
    logger.setLevel(logging.INFO)
    s_handler = logging.StreamHandler()    	
    f_handler = logging.FileHandler(f'{outdir}/tuning.log', 'a')
    s_handler.setLevel(level=logging.INFO)
    f_handler.setLevel(level=logging.INFO)    
    s_handler.setFormatter(formatter)
    f_handler.setFormatter(formatter)
    logger.addHandler(s_handler)
    logger.addHandler(f_handler)
    
    return logger       