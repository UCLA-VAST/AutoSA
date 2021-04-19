import numpy as np

import utils

class Tuner(object):
    def __init__(self, task, cst, obj, logger, max_epochs, max_time):
        self.task = task
        self.cst = cst
        self.obj = obj
        self.logger = logger
        self.max_epochs = max_epochs
        self.max_time = max_time
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
    def __init__(self, task, cst, obj, logger, max_epochs, max_time, params):
        super().__init__(task, cst, obj, logger, max_epochs, max_time)
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

    def select_parents(self, population, fitness, num_parents):
        """ Select "num_parents" parents with the highest fitness score.
        """        
        fitness_idx_sorted = np.argsort(-fitness)        
        parents = population[fitness_idx_sorted[:num_parents]][:]
        return parents

    def crossover(self, pool, num_children):
        pass

    def mutation(self, pool):
        pass

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
        self.counter.start_counter('Search Time')   
        if self.stop_criteria == "time":
            self.counter.start_counter('time')

        # Init the stats
        num_pop = int(self.params["population_size"])
        num_gen = int(self.max_epoch // num_pop)        
        num_parents = int(num_pop * self.params["parents_ratio"])
        self.logger.info(f'Number of generations: {num_gen}')
        self.logger.info(f'Number of population: {num_pop}')
        self.logger.info(f'Number of parents: {num_parents}')

        # Init the population
        population = np.empty((num_pop, 6), dtype=int)
        if "ancestor" in self.params and self.params["ancestor"] != None:
            pass
        else:
            # Initialize the population randomly
            pop_cnt = 0
            while pop_cnt < num_pop:                
                task_params = self.task.generate_random_sample() # TODO
                param_arr = []
                for param in self.design.params_config:
                    if param["tunable"]:
                        param_arr.append(task_params[param["name"]])
                population[pop_cnt] = np.array(param_arr, dtype=int)
                pop_cnt += 1                
        fitness = np.empty(num_pop, dtype=float)
        for i in range(num_pop):
            idv = population[i]
            task_params = {}
            idx = 0
            for param in self.design.params_config:
                if param["tunable"]:
                    task_params[param["name"]] = idv[idx]
                    idx += 1
            reward, used_constraint = self.task.evaluate(task_params, self.obj) # TODO
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
                idx = 0
                for param in self.design.params_config:
                    if param["tunable"]:
                        task_params[param["name"]] = idv[idx]
                        idx += 1
                reward, used_constraint = self.task.evaluate(task_params, self.obj) # TODO
                if self.overuse_constraint(used_constraint):                
                    reward = 0
                fitness[i] = reward
                # Update the record
                if reward > self.best_reward:
                    self.best_reward = reward
                    self.best_task_params = task_params
                    self.logger.info(f'Epoch {self.epoch}: new best reward: {self.best_reward} ({1/self.best_reward:.0f})')
                    self.best_search_record = utils.SearchRecord().extract_from_tuner(self)
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

    tuner = GeneticTuner(task, cst, obj, logger, max_epochs, max_time, tuner_params) # TODO
    tuner.search()
    search_record = utils.SearchRecord().extract_from_tuner(tuner)

    return search_record