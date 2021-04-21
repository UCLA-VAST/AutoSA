import numpy as np

import utils
import random

class Tuner(object):
    def __init__(self, task, cst, obj, logger, max_epoch, max_time):
        self.task = task
        self.cst = cst
        self.obj = obj
        self.logger = logger
        self.max_epoch = max_epoch
        self.max_time = max_time
        self.best_reward = 0
        self.best_task_params = None
        self.best_search_record = utils.SearchRecord().reset()        

    def overuse_constraint(self, used_cst):
        if not used_cst:
            # If constraint doesn't exist, return True to exclude this design
            return True

        if used_cst['BRAM18K'] > self.cst.hw_cst['BRAM18K']:            
            return True
        if used_cst['DSP'] > self.cst.hw_cst['DSP']:            
            return True
        return False

class GeneticTuner(Tuner):
    def __init__(self, task, cst, obj, logger, max_epoch, max_time, params):
        super().__init__(task, cst, obj, logger, max_epoch, max_time)        
        self.params = params
        self.epoch = 0
        if max_epoch > 0:
            self.stop_criteria = "epoch"
            self.max_epoch = max_epoch
        else:
            self.stop_criteria = "time"
            self.max_time = max_time
        self.counter = utils.PerfCounter(self.logger)
        self.search_time = None
        self.param_idx_map = {}
        self.idx_param_map = {}

    def select_parents(self, population, fitness, num_parents):
        """ Select "num_parents" parents with the highest fitness score.
        """        
        fitness_idx_sorted = np.argsort(-fitness)        
        parents = population[fitness_idx_sorted[:num_parents]][:]
        return parents

    def crossover(self, pool, num_children):
        """ Perform single-point crossover.
        """
        children = np.empty((num_children, len(self.task.design.params_config["tunable"])))
        # Build the parameter dependecy chain
        param_deps = {}
        param_cnt = 0
        for p, param in self.task.design.params_config["tunable"].items():
            if "divisors" in param:
                param_deps[param["name"]] = param["divisors"][0]
                param_cnt += 2
        if param_cnt != len(self.task.design.params_config["tunable"]):
            raise RuntimeError("Not all tuning parameters can be handled by crossover")
        #print(param_deps)        
        for i in range(num_children):
            parents_idx = [i % pool.shape[0], np.random.randint(0, pool.shape[0])]
            #print(parents_idx)
            #print(pool[parents_idx[0]][:])
            #print(pool[parents_idx[1]][:])
            for param in param_deps:
                idx = np.random.randint(0, 2)
                #print(idx)
                children[i][self.param_idx_map[param]] = pool[parents_idx[idx]][self.param_idx_map[param]]
                children[i][self.param_idx_map[param_deps[param]]] = pool[parents_idx[idx]][self.param_idx_map[param_deps[param]]]
            #print(children[i][:])
            #exit(0)

        return children

    def mutation(self, pool):
        """ Perform mutation
        """
        for p_idx in range(pool.shape[0]):
            if random.random() < self.params["mutation_probability"]:
                if random.random() < self.params["epsilon"]:
                    task_params = self.task.generate_random_sample()
                    for i in range(pool.shape[1]):
                        pool[p_idx][i] = task_params[self.idx_param_map[i]]
                else:
                    idv = pool[p_idx][:]
                    task_params = {}                    
                    for p, param in self.task.design.params_config["tunable"].items():                
                        task_params[param["name"]] = idv[self.param_idx_map[param["name"]]]
                    for p, param in self.task.design.params_config["external"].items():
                        task_params[param["name"]] = self.task.task["params"][param["name"]]
                    # Build the chains
                    # [{"params": [p0, p3, p7], "factors": [ceil(p0/p3), p3/p7, p7]}, {}]
                    split_chains = []
                    for p, param in self.task.design.params_config["external"].items():
                        chain = {"params": [param["name"]], "factors": []}
                        cur_param = param                                                
                        while "split_by" in cur_param:
                            #print(self.task.design.params_config["tunable"][cur_param["split_by"]])
                            if "divisors" in self.task.design.params_config["tunable"][cur_param["split_by"]] \
                                and cur_param["name"] in self.task.design.params_config["tunable"][cur_param["split_by"]]["divisors"]:
                                div = 1
                            else:
                                div = 0
                            chain["params"].append(cur_param["split_by"])
                            if div:
                                factor = np.ceil(task_params[cur_param["name"]] / task_params[cur_param["split_by"]])
                            else:
                                factor = task_params[cur_param["name"]] / task_params[cur_param["split_by"]]                            
                            chain["factors"].append(int(factor))                            
                            cur_param = self.task.design.params_config["tunable"][cur_param["split_by"]]                        
                        chain["factors"].append(int(task_params[cur_param["name"]]))
                        split_chains.append(chain)
                    
                    # Mutation
                    for chain in split_chains:
                        if len(chain["factors"]) <= 1:
                            continue
                        src_idx, dst_idx = random.sample(range(0, len(chain["factors"])), 2)
                        mutation_policy_probs = [0.2, 0, 0.8]
                        mutation_policy_probs = np.cumsum(mutation_policy_probs)
                        if random.random() < mutation_policy_probs[0]:
                            if chain["factors"][dst_idx] == 1:
                                continue
                            inc_stride = max(1, int(chain["factors"][src_idx] * random.random() * 1.0))
                            dec_stride = max(1, int(chain["factors"][dst_idx] - chain["factors"][src_idx] * chain["factors"][dst_idx] / (chain["factors"][src_idx] + inc_stride)))
                            chain["factors"][src_idx] += inc_stride                        
                            chain["factors"][dst_idx] -= dec_stride
                            chain["factors"][dst_idx] = max(1, chain["factors"][dst_idx])          
                        elif random.random() < mutation_policy_probs[1]:
                            pass
                        else:
                            factor = chain["factors"][src_idx]
                            if factor == 1:
                                continue
                            divs = utils.factorization(factor)
                            div = random.choice(divs)
                            chain["factors"][src_idx] /= div
                            chain["factors"][dst_idx] *= div

                    # Revert to the params
                    # [{"params": [p0, p3, p7], "factors": [ceil(p0/p3), p3/p7, p7]}, {}]
                    for chain in split_chains:
                        factor = chain["factors"][-1]
                        param = chain["params"][-1]                        
                        if param in self.param_idx_map:
                            pool[p_idx][self.param_idx_map[param]] = factor
                        for idx in range(len(chain["factors"]) - 2, -1, -1):
                            param = chain["params"][idx]
                            factor *= chain["factors"][idx]
                            if param in self.param_idx_map:
                                pool[p_idx][self.param_idx_map[param]] = factor
        
        return pool             

    def search(self):
        """ Search the design space using genetic algorithms.

        The algorithm is configured by several parameters.
        @ population_size: the number of trial solutions in each epoch.
        @ mutation_probability: the chance of each gene in each individual solution
        to be replaced by a random value.
        @ crossover_probability: the chance of an existed solution to pass its genome
        to new trial solutions.
        @ parents_ratio: the ratio of population filled by the members of the previous
        generation.
        """     
        self.counter.init_counter('Search Time')   
        if self.stop_criteria == "time":
            self.counter.init_counter('time')

        # Init the stats
        num_pop = int(self.params["population_size"])
        num_gen = int(self.max_epoch // num_pop)        
        num_parents = int(num_pop * self.params["parents_ratio"])
        self.logger.info(f'Number of generations: {num_gen}')
        self.logger.info(f'Number of population: {num_pop}')
        self.logger.info(f'Number of parents: {num_parents}')

        # Init the population
        population = np.empty((num_pop, len(self.task.design.params_config["tunable"])), dtype=int)
        if "ancestor" in self.params and self.params["ancestor"] != None:
            pass
        else:
            # Initialize the population randomly
            pop_cnt = 0
            while pop_cnt < num_pop:                
                task_params = self.task.generate_random_sample()
                param_arr = []
                for p, param in self.task.design.params_config["tunable"].items():                    
                    param_arr.append(task_params[param["name"]])
                population[pop_cnt] = np.array(param_arr, dtype=int)
                pop_cnt += 1                
        idx = 0
        for p, param in self.task.design.params_config["tunable"].items():
            self.param_idx_map[param["name"]] = idx
            self.idx_param_map[idx] = param["name"]
            idx += 1

        fitness = np.empty(num_pop, dtype=float)
        for i in range(num_pop):
            idv = population[i]
            task_params = {}
            for p, param in self.task.design.params_config["tunable"].items():
                task_params[param["name"]] = idv[self.param_idx_map[param["name"]]]                    
            for p, param in self.task.design.params_config["external"].items():
                task_params[param["name"]] = self.task.task["params"][param["name"]]
            reward, used_constraint = self.task.evaluate(task_params, self.obj)
            if self.overuse_constraint(used_constraint):                
                reward = 0
            fitness[i] = reward

        while True:
            # Select the parents
            parents = self.select_parents(population, fitness, num_parents)
            # Crossover
            children = self.crossover(parents, num_pop - num_parents)
            # Mutation            
            children = self.mutation(children) 
            # Compose the new generation
            population[0:parents.shape[0], :] = parents
            population[parents.shape[0]:, :] = children      
            # Update the fitness
            for i in range(num_pop):
                idv = population[i]
                task_params = {}                
                for p, param in self.task.design.params_config["tunable"].items():
                    task_params[param["name"]] = idv[self.param_idx_map[param["name"]]]                    
                for p, param in self.task.design.params_config["external"].items():
                    task_params[param["name"]] = self.task.task["params"][param["name"]]
                #print(task_params)
                task_params = self.task.adjust_params(task_params)
                #if task_params["p3"] % task_params["p7"] != 0:
                #    print(task_params)
                #    exit(0)
                #print(task_params)                
                reward, used_constraint = self.task.evaluate(task_params, self.obj)
                if self.overuse_constraint(used_constraint):                
                    reward = 0
                fitness[i] = reward
                # Update the record
                if reward > self.best_reward:
                    self.best_reward = reward
                    self.best_cst = used_constraint
                    self.best_task_params = task_params
                    self.logger.info(f'Epoch {self.epoch}: new best reward: {self.best_reward} ({1/self.best_reward:.0f})')
                    self.best_search_record = utils.SearchRecord().extract_from_tuner(self)
            #exit(0)
            self.epoch += num_pop
            if self.stop_criteria == "epoch" and epoch > self.max_epoch:
                break
            if self.stop_criteria == "time":
                self.counter.update_counter('time')
                if self.counter.get_counter('time') > self.max_time:
                    break

        self.counter.update_counter('Search Time')   
        self.search_time = self.counter.get_counter('Search Time')
        return

def genetic_search(task, cst, obj, logger, max_epochs, max_time):
    tuner_params = {
        "population_size": 200,\
        "mutation_probability": 0.5,\
        "parents_ratio": 0.3,\
        "epsilon": 0.1,\
        "ancestor": None            
    }

    tuner = GeneticTuner(task, cst, obj, logger, max_epochs, max_time, tuner_params)
    tuner.search()
    search_record = utils.SearchRecord().extract_from_tuner(tuner)    

    return search_record