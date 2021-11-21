import torch.nn as nn
import numpy as np
import random
import bisect
import copy

import torch
import torch.optim as optim
import torch.nn.functional as F
from torch.distributions import Categorical

import utils

device = torch.device("cuda:0" if torch.cuda.is_available() else "cpu")
LR_ACTOR = 1e-3 # learning rate of the actor
GAMMA = 0.9  # discount factor
EPSILON = 2**(-12)
CLIPPING_MODEL = 100

class RLEnv():
    def __init__(self, search_task, cst, param_idx_map, idx_param_map, search_obj, dim_size, n_action_steps, action_size):
        """    
        search_task: search task object
        dim_size: dimension of the problem space, 3 for GEMM
        n_action_steps: dimension of the action vector, 6 for GEMM
        """
        self.search_task = search_task
        self.cst = cst
        self.param_idx_map = param_idx_map
        self.idx_param_map = idx_param_map
        self.search_obj = search_obj
        self.dim_size = dim_size
        self.n_action_steps = n_action_steps
        self.action_size = action_size
        action_bound, action_bottom = self.build_action_space()
        self.action_bound = action_bound
        self.action_bottom = action_bottom
        
        self.state = np.array([0.5]*n_action_steps) # (action vector)        
        # Sum of adjusted rewards
        self.adjusted_epoch_rewards = 0        
        # Sum of raw rewards
        self.epoch_rewards = 0
        self.prev_epoch_rewards = 0        
        self.sig = 1
        # The minimal reward during the whole training process
        self.min_reward = float("inf")
        self.epoch = 0
        self.best_epoch_rewards = float("-inf")        
        # Keep track of best rewards during the training process
        self.rewards_log = []
        self.best_rewards_log = []

    def reset(self):
        """ Reset the state of the environment.

        """
        # (i_t1, j_t1, k_t1, i_t2, j_t2, k_t2)
        self.state = np.array([0]*6, dtype=np.float)
        self.adjusted_epoch_rewards = 0        
        self.epoch_rewards = 0        
        self.sig = 1
        self.sol = []
        infos = {}

        return self.state, infos

    def get_state(self):
        return self.state

    def set_constraint(self, cst):
        """ Set up hw constraint.
        """
        self.cst = cst

    def build_action_space(self):
        action_bound = [self.search_task.workload["params"]["i"], 
                        self.search_task.workload["params"]["j"], 
                        self.search_task.workload["params"]["k"], 
                        self.search_task.workload["params"]["i"], 
                        self.search_task.workload["params"]["j"], 
                        min(256 // self.search_task.dw, 64, self.search_task.workload["params"]["k"])]
        action_bottom = [1 for a in range(self.n_action_steps)]
        return action_bound, action_bottom

    def overuse_constraint(self, used_cst):
        score = 0
        if not used_cst:
            # If constraint doesn't exist, return True to exclude this design
            return True, score

        overuse = False

        if used_cst['BRAM18K'] > self.cst.hw_cst['BRAM18K']:
            score += 0.5 * (used_cst['BRAM18K'] - self.cst.hw_cst['BRAM18K']) / self.cst.hw_cst['BRAM18K']
            overuse = True
        if used_cst['URAM'] > self.cst.hw_cst['URAM']:
            score += 0.5 * (used_cst['URAM'] - self.cst.hw_cst['URAM']) / self.cst.hw_cst['URAM']
            overuse = True    
        if used_cst['DSP'] > self.cst.hw_cst['DSP']:
            score += 0.5 * (used_cst['DSP'] - self.cst.hw_cst['DSP']) / self.cst.hw_cst['DSP']
            overuse = True
        
        return overuse, score

    def update_total_reward_constraint(self, constraint, reward):
        """ Accumulate the resource and rewards in one epoch.
        Currently we only consider rewards.
        """        
        self.epoch_rewards += reward

    def get_reward(self, task_params):
        """ Call the cost models to get the reward for current solution.

        Returns
        -------
        reward:
            The adjusted reward for the current solution.
        constraint:
            The used constraint of the current solution.
        reward_raw:
            The unadjusted reward for the current solution.
        """
        reward, used_constraint, reward_meta = self.search_task.evaluate(task_params, self.search_obj)
        #reward, constraint = self.search_task.evaluate(sol)        
        if reward == None or reward == 0:
            return -1, None, -1, None        
        reward_raw = reward        
        self.min_reward = min(self.min_reward, reward_raw)
        # Adjust the reward by subtracting the minimal reward found so far
        # to stabilize the training.
        reward -= self.min_reward
        self.adjusted_epoch_rewards += reward

        return reward, used_constraint, reward_raw, reward_meta

    def norm_state(self, T):
        """ Normalize the state to the range of [-1, 1] to stabilize the training.
        The input state is in the range of [0, 1].
        """
        T[:-1] = (T[:-1] - 0.5) * 2
        return T

    def update_mode_and_step(self):
        pass        

    def update_reward_epoch(self):
        if self.epoch_rewards > self.best_epoch_rewards:
            self.best_epoch_rewards = self.epoch_rewards

    def update_best_reward_list(self, succeed):
        """ Update the information
        """
        self.epoch += 1
        # If the current epoch fails, we roll back to the reward in the last successful epoch.
        self.epoch_rewards = self.prev_epoch_rewards if not succeed else self.prev_epoch_rewards
        self.prev_epoch_rewards = self.epoch_rewards
        self.rewards_log.append(self.epoch_rewards)
        self.best_rewards_log.append(self.best_epoch_rewards)

    def update_reward_impt(self, done):
        impt = None        
        return impt

    def convert_action_to_vals(self, action):
        """ Convert the actions to the real tiling factors.
        """
        action_norm = np.array([float(a) / self.action_size for a in action]).clip(0, 1)
        # i_t1, j_t1, k_t1
        for i in range(3):
            action[i] = int(action[i] / self.action_size * self.action_bound[i])
        # i_t2, j_t2, k_t2
        for i in range(3):
            action[i] = int(action[i] / self.action_size * self.action_bound[i])

        task_params = {}
        for p, param in self.search_task.design.params_config["tunable"].items():
            task_params[param["name"]] = action[self.param_idx_map[param["name"]]]
        for p, param in self.search_task.design.params_config["external"].items():
            task_params[param["name"]] = self.search_task.workload["params"][param["name"]]
        task_params = self.search_task.adjust_params(task_params)
        task_params = self.search_task.design.infer_params(task_params)

        action = []
        for p, param in self.search_task.design.params_config["tunable"].items():
            action.append(task_params[param["name"]])

        return action, action_norm

    def step(self, action):
        infos = {}
        infos['succeed'] = 0        
        done = 0
        action = action.cpu().numpy().flatten()        
        # Scale the action back to the real tiling factors
        # Actions are in the levels of 1 to self.action_size.
        # We will need to scale them back to the corresponding tiling factors.
        action_val = [int(a) + 1 for a in action]
        action_val, action_norm = self.convert_action_to_vals(action_val)        
        # Compose the solution
        task_params = {}
        for p, param in self.search_task.design.params_config["tunable"].items():
            task_params[param["name"]] = action_val[self.param_idx_map[param["name"]]]
        for p, param in self.search_task.design.params_config["external"].items():
            task_params[param["name"]] = self.search_task.workload["params"][param["name"]]
        task_params = self.search_task.adjust_params(task_params)
        task_params = self.search_task.design.infer_params(task_params)
        infos['sol'] = task_params
        reward, used_constraint, reward_raw, reward_meta = self.get_reward(task_params)
        infos['cst'] = used_constraint        
        infos['reward_meta'] = reward_meta
        self.update_total_reward_constraint(used_constraint, reward_raw)
        self.sol.append((copy.deepcopy(action_val)))

        # Penalize the solution that overuses the resource
        overuse, overuse_score = self.overuse_constraint(used_constraint)
        if overuse:            
            reward = (-self.adjusted_epoch_rewards + reward) * overuse_score
            reward_raw = 0
            done = 1
        if reward == -1:
            done = 1
        infos['reward_raw'] = reward_raw

        # Normalize the state to [-1,1]
        self.state = self.norm_state(action_norm)
        if not done:
            infos["succeed"] = 1
            done = 1
            self.update_reward_epoch()
        infos["epoch_rewards"] = self.epoch_rewards
        self.update_best_reward_list(infos["succeed"]) if done else None        
        impt = self.update_reward_impt(done)        
        return self.state, reward, done, infos, self.sig, impt

class RLAgent():
    def __init__(self, dim_size, n_action_steps, action_size, seed, batch, decay=0.95):
        """
        Parameters
        ----------
        dim_size:
            problem space dimensions, 3 for GEMM
        n_action_steps:
            dimension of one action, 6 for GEMM
        action_size:
            levels of each action step in one action
        seed:
            random seed
        """
        # Attributes
        self.dim_size = dim_size
        self.action_size = action_size
        self.n_action_steps = n_action_steps        
        # [n_action_steps]
        self.state_div = [n_action_steps]
        self.state_div = np.cumsum(self.state_div)
        self.seed = random.seed(seed)
        self.batch = batch

        # Actor
        self.actor = Actor(dim_size, n_action_steps, action_size, seed).to(device)
        self.actor_optimizer = optim.Adam(self.actor.parameters(), lr=LR_ACTOR)
        self.scheduler = optim.lr_scheduler.ReduceLROnPlateau(self.actor_optimizer, factor=0.9, min_lr=1e-6)
        self.decay = decay
        self.epoch = 0

        # Book-keeping
        self.saved_log_probs = []
        self.rewards = []
        self.baseline = None
        self.lowest_reward = 0
        self.best_epoch_reward = float("-Inf")
        self.has_succeeed_history = False
        self.bad_counts = 0

    def reset(self):
        """ Rest the internal status
        """
        self.saved_log_probs = []
        self.rewards = []

    def adjust_lr(self, ratio, min_lr=1e-8):
        """ Adjust the learning rate
        """
        for param_group in self.actor_optimizer.param_groups:
            param_group['lr'] = max(min_lr, param_group['lr'] * ratio)

    def act(self, state, infos, eps=0.0, temperature=1):
        """ Perform one action given the current state.
        """        
        actions = state[0:self.state_div[0]]

        # Convert them to pytorch data structs        
        actions = torch.from_numpy(actions).type(torch.FloatTensor).to(device)        

        # Run the policy network        
        (p) = self.actor(actions, temperature=temperature)
        #print(p)
        m = Categorical(p)
        #print(m)
        action = m.sample()
        
        if random.random() < eps:
            action2 = action.data + 1 if random.random() < 0.5 else action.data - 1
            action2 = torch.from_numpy(np.array([action2]))
            action2 = torch.clamp(action2, 0, p.size(1)-1)
            return action2.data, m.log_prob(action2)
        else:
            return action.data, m.log_prob(action)

    def step(self, state, actions, log_prob, reward, next_state, done, sig, impt, infos):
        """ Update and train the policy network
        """
        self.rewards.append(reward)
        #print('returned', reward)
        self.saved_log_probs.append(log_prob)        
        self.epoch += 1        
        if self.epoch == self.batch:
            self.learn(GAMMA, impt, infos)

    def impt_adj_reward(self, reward, impt):
        """ Adjust the rewards
        """
        if impt is not None:
            reward[:len(impt)] = reward[:len(impt)] * impt
        return reward

    def learn(self, gamma, impt, infos):
        """ Train the policy network
        """
        rewards = np.array(self.rewards)
        # Normalize the rewards
        rewards = (rewards - rewards.mean()) / (rewards.std() + EPSILON)
        # Adjust the rewards
        rewards = self.impt_adj_reward(rewards, impt)
        dis_rewards = []
        R = 0
        for r in rewards[::-1]:
            R = r + gamma * R
            dis_rewards.insert(0, R)
        dis_rewards = np.array(dis_rewards)
        dis_rewards = (dis_rewards - dis_rewards.mean()) / (dis_rewards.std() + EPSILON)

        policy_loss = []
        for log_prob, r in zip(self.saved_log_probs, dis_rewards):
            policy_loss.append(-log_prob * r)
        policy_loss = torch.cat(policy_loss).sum()

        self.actor_optimizer.zero_grad()
        policy_loss.backward()
        torch.nn.utils.clip_grad_norm_(self.actor.parameters(), CLIPPING_MODEL)
        self.actor_optimizer.step()
        self.reset()

class Actor(nn.Module):
    def __init__(self, dim_size, n_action_steps, action_size, seed, h_size=128, hidden_dim=10):
        """
        We implement a simple FC networks as the policy network.

        Parameters
        ----------
        dim_size:
            The problem space dimension
        n_action_steps:
            Number of the action steps, 6 for GEMM (i_t1, j_t1, k_t1, i_t2, j_t2, k_t2)
        action_size:
            Level of action steps, max(i, j, k) for GEMM
        h_size:
            FC layer dimension
        hidden_dim:
            dimensions of the encoder layer
        """
        super().__init__()
        self.seed = torch.manual_seed(seed)

        # Encoder
        self.encoder_action = nn.Linear(n_action_steps, hidden_dim)

        # hidden_dim -> h_size
        self.fc11 = nn.Linear(hidden_dim, h_size)
        # h_size -> h_size
        self.fc12 = nn.Linear(h_size, h_size)
        # h_size -> h_size
        self.fc13 = nn.Linear(h_size, h_size)

        self.fc21 = nn.Linear(h_size, action_size)
        self.fc22 = nn.Linear(h_size, action_size)
        self.fc23 = nn.Linear(h_size, action_size)
        self.fc24 = nn.Linear(h_size, action_size)
        self.fc25 = nn.Linear(h_size, action_size)
        self.fc26 = nn.Linear(h_size, action_size)

        self.output1 = nn.Linear(action_size, action_size)
        self.output2 = nn.Linear(action_size, action_size)
        self.output3 = nn.Linear(action_size, action_size)
        self.output4 = nn.Linear(action_size, action_size)
        self.output5 = nn.Linear(action_size, action_size)
        self.output6 = nn.Linear(action_size, action_size)

        self.decoder = [self.fc21, self.fc22, self.fc23, self.fc24, self.fc25, self.fc26]
        self.n_action_steps = n_action_steps

    def forward(self, action_steps, temperature=1):
        """
        Network forward inference.

        Paramters
        ---------
        """
        x1 = self.encoder_action(action_steps)
        x1 = x1.unsqueeze(0)

        x = x1
        x = F.relu(self.fc11(x))
        x = F.relu(self.fc12(x))
        x = F.relu(self.fc13(x))

        # i1
        decoder = self.decoder[0]
        x1 = F.relu(decoder(x))
        x1 = self.output1(x1)
        x1 = F.softmax(x1/temperature, dim=1)

        # j1
        decoder = self.decoder[1]
        x2 = F.relu(decoder(x))
        x2 = self.output2(x2)
        x2 = F.softmax(x2/temperature, dim=1)

        # k1
        decoder = self.decoder[2]
        x3 = F.relu(decoder(x))
        x3 = self.output3(x3)
        x3 = F.softmax(x3/temperature, dim=1)

        # i2
        decoder = self.decoder[3]
        x4 = F.relu(decoder(x))
        x4 = self.output4(x4)
        x4 = F.softmax(x4/temperature, dim=1)

        # j2
        decoder = self.decoder[4]
        x5 = F.relu(decoder(x))
        x5 = self.output5(x5)
        x5 = F.softmax(x5/temperature, dim=1)

        # k2
        decoder = self.decoder[5]
        x6 = F.relu(decoder(x))
        x6 = self.output6(x6)
        x6 = F.softmax(x6/temperature, dim=1)

        # Return the concatenated (x1, x2, x3, x4, x5, x6)
        x = torch.cat((x1, x2, x3, x4, x5, x6), dim=0)

        return (x)