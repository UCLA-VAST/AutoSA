from math import ceil
import numpy as np
import random
import utils

def est_resource(params):
	i, j, k, i_t1, j_t1, k_t1, i_t2, j_t2, k_t2, p9, p10, p11, p12 = params["i"], params["j"], params["k"], params["i_t1"], params["j_t1"], params["k_t1"], params["i_t2"], params["j_t2"], params["k_t2"], params["p9"], params["p10"], params["p11"], params["p12"]

	# DSP
	DSP = ((i_t1/i_t2)*(j_t1/j_t2)) * k_t2 * 5

	# BRAM18K
	def est_BRAM18K(ele_size, ele_num, pack):
		return ceil(ele_size*8*pack / 36) * ceil(ele_num/pack/512)

	res_meta = {}
	A_IO_L2_in_unit_memory = est_BRAM18K(4, (i_t2*k_t1), p9)
	res_meta["A_IO_L2_in"] = {"ele_size": 4, "buf_size": (i_t2*k_t1), "data_pack_factor": 1, "num": (i_t1/i_t2)}
	res_meta["A_IO_L2_in"]["num"] *= 2
	B_IO_L2_in_unit_memory = est_BRAM18K(4, (j_t2*k_t1), p10)
	res_meta["B_IO_L2_in"] = {"ele_size": 4, "buf_size": (j_t2*k_t1), "data_pack_factor": 1, "num": (j_t1/j_t2)}
	res_meta["B_IO_L2_in"]["num"] *= 2
	C_drain_IO_L1_out_unit_memory = est_BRAM18K(4, (i_t2*j_t2), p12)
	res_meta["C_drain_IO_L1_out"] = {"ele_size": 4, "buf_size": (i_t2*j_t2), "data_pack_factor": 1, "num": ((j_t1/j_t2)*(i_t1/i_t2))}
	PE_unit_memory = est_BRAM18K(4, (i_t2*j_t2), 1)
	res_meta["PE"] = {"ele_size": 4, "buf_size": (i_t2*j_t2), "data_pack_factor": 1, "num": ((i_t1/i_t2)*(j_t1/j_t2))}
	BRAM18K = A_IO_L2_in_unit_memory * 2 * (i_t1/i_t2) + B_IO_L2_in_unit_memory * 2 * (j_t1/j_t2) + C_drain_IO_L1_out_unit_memory * 1 * ((j_t1/j_t2)*(i_t1/i_t2)) + PE_unit_memory * 1 * ((i_t1/i_t2)*(j_t1/j_t2))

	# URAM
	URAM = 0

	res = {"DSP": DSP, "BRAM18K": BRAM18K, "URAM": URAM}
	res['A_IO_L2_in_unit_memory'] = A_IO_L2_in_unit_memory
	res['B_IO_L2_in_unit_memory'] = B_IO_L2_in_unit_memory
	res['C_drain_IO_L1_out_unit_memory'] = C_drain_IO_L1_out_unit_memory
	res['PE_unit_memory'] = PE_unit_memory

	return res, res_meta

def est_latency(params):
	i, j, k, i_t1, j_t1, k_t1, i_t2, j_t2, k_t2, p9, p10, p11, p12 = params["i"], params["j"], params["k"], params["i_t1"], params["j_t1"], params["k_t1"], params["i_t2"], params["j_t2"], params["k_t2"], params["p9"], params["p10"], params["p11"], params["p12"]

	A_IO_L2_in_single_latency = ((i_t1/i_t2) * (i_t2*k_t1/p9) + 1)
	A_IO_L3_in_single_latency = (i_t1/i_t2) * (i_t2*k_t1/min(p9, 512/8/4))
	B_IO_L2_in_single_latency = ((j_t1/j_t2) * (j_t2*k_t1/p10) + 1)
	B_IO_L3_in_single_latency = (j_t1/j_t2) * (j_t2*k_t1/min(p10, 512/8/4))
	latency_prologue = max(A_IO_L2_in_single_latency, A_IO_L3_in_single_latency, B_IO_L2_in_single_latency, B_IO_L3_in_single_latency)

	C_drain_IO_L1_out_single_latency = (i_t1/i_t2) * (i_t2*j_t2/p12)
	C_drain_IO_L2_out_single_latency = (j_t1/j_t2) * (i_t1/i_t2) * (i_t2*j_t2/p12)
	C_drain_IO_L3_out_single_latency = (j_t1/j_t2) * (i_t1/i_t2) * (i_t2*j_t2/min(p12, 512/8/4))
	latency_epilogue = max(C_drain_IO_L1_out_single_latency, C_drain_IO_L2_out_single_latency, C_drain_IO_L3_out_single_latency)

	A_IO_L2_in_latency = ceil((i/i_t1)) * ceil((j/j_t1)) * ceil((k/k_t1)) * (max((i_t1/i_t2) * (i_t2*k_t1/p9), (k_t1/k_t2) * j_t2 * i_t2 * 1) + 1)
	A_IO_L3_in_latency = ceil((i/i_t1)) * ceil((j/j_t1)) * ceil((k/k_t1)) * (i_t1/i_t2) * (i_t2*k_t1/min(p9, 512/8/4))
	B_IO_L2_in_latency = ceil((i/i_t1)) * ceil((j/j_t1)) * ceil((k/k_t1)) * (max((j_t1/j_t2) * (j_t2*k_t1/p10), (k_t1/k_t2) * j_t2 * i_t2 * 1) + 1)
	B_IO_L3_in_latency = ceil((i/i_t1)) * ceil((j/j_t1)) * ceil((k/k_t1)) * (j_t1/j_t2) * (j_t2*k_t1/min(p10, 512/8/4))
	C_drain_IO_L1_out_latency = ceil((i/i_t1)) * ceil((j/j_t1)) * ((i_t1/i_t2) * (i_t2*j_t2/p12) + j_t2 * i_t2 * 1)
	C_drain_IO_L2_out_latency = ceil((i/i_t1)) * ceil((j/j_t1)) * (j_t1/j_t2) * (i_t1/i_t2) * (i_t2*j_t2/p12)
	C_drain_IO_L3_out_latency = ceil((i/i_t1)) * ceil((j/j_t1)) * (j_t1/j_t2) * (i_t1/i_t2) * (i_t2*j_t2/min(p12, 512/8/4))
	PE_latency = ceil((i/i_t1)) * ceil((j/j_t1)) * ceil((k/k_t1)) * (k_t1/k_t2) * j_t2 * i_t2 * 1
	latency_main = max(A_IO_L2_in_latency, A_IO_L3_in_latency, B_IO_L2_in_latency, B_IO_L3_in_latency, C_drain_IO_L1_out_latency, C_drain_IO_L2_out_latency, C_drain_IO_L3_out_latency, PE_latency)

	latency = latency_prologue + latency_main + latency_epilogue

	# Meta information, used for conv fusion only
	latency_meta = {"latency_prologue": {}, "latency_main": {}, "latency_epilogue": {}}
	latency_meta["latency_prologue"]["A_IO_L2_in_single_latency"] = A_IO_L2_in_single_latency
	latency_meta["latency_prologue"]["A_IO_L3_in_single_latency"] = A_IO_L3_in_single_latency
	latency_meta["latency_prologue"]["B_IO_L2_in_single_latency"] = B_IO_L2_in_single_latency
	latency_meta["latency_prologue"]["B_IO_L3_in_single_latency"] = B_IO_L3_in_single_latency
	latency_meta["latency_epilogue"]["C_drain_IO_L1_out_single_latency"] = C_drain_IO_L1_out_single_latency
	latency_meta["latency_epilogue"]["C_drain_IO_L2_out_single_latency"] = C_drain_IO_L2_out_single_latency
	latency_meta["latency_epilogue"]["C_drain_IO_L3_out_single_latency"] = C_drain_IO_L3_out_single_latency
	latency_meta["latency_main"]["A_IO_L2_in_latency"] = A_IO_L2_in_latency
	latency_meta["latency_main"]["A_IO_L3_in_latency"] = A_IO_L3_in_latency
	latency_meta["latency_main"]["B_IO_L2_in_latency"] = B_IO_L2_in_latency
	latency_meta["latency_main"]["B_IO_L3_in_latency"] = B_IO_L3_in_latency
	latency_meta["latency_main"]["C_drain_IO_L1_out_latency"] = C_drain_IO_L1_out_latency
	latency_meta["latency_main"]["C_drain_IO_L2_out_latency"] = C_drain_IO_L2_out_latency
	latency_meta["latency_main"]["C_drain_IO_L3_out_latency"] = C_drain_IO_L3_out_latency
	latency_meta["latency_main"]["PE_latency"] = PE_latency
	return latency, latency_meta

def est_activity(params):
	i, j, k, i_t1, j_t1, k_t1, i_t2, j_t2, k_t2, p9, p10, p11, p12 = params["i"], params["j"], params["k"], params["i_t1"], params["j_t1"], params["k_t1"], params["i_t2"], params["j_t2"], params["k_t2"], params["p9"], params["p10"], params["p11"], params["p12"]

	activity = {}
	activity["off_chip_acc_num_meta"] = {}
	off_chip_acc_num = 0
	A_IO_L3_in_off_chip_acc_num = ceil((i/i_t1)) * ceil((j/j_t1)) * ceil((k/k_t1)) * (i_t1/i_t2) * (i_t2*k_t1)
	activity["off_chip_acc_num_meta"]["A_IO_L3_in"] = A_IO_L3_in_off_chip_acc_num
	off_chip_acc_num += A_IO_L3_in_off_chip_acc_num
	B_IO_L3_in_off_chip_acc_num = ceil((i/i_t1)) * ceil((j/j_t1)) * ceil((k/k_t1)) * (j_t1/j_t2) * (j_t2*k_t1)
	activity["off_chip_acc_num_meta"]["B_IO_L3_in"] = B_IO_L3_in_off_chip_acc_num
	off_chip_acc_num += B_IO_L3_in_off_chip_acc_num
	C_drain_IO_L3_out_off_chip_acc_num = ceil((i/i_t1)) * ceil((j/j_t1)) * (j_t1/j_t2) * (i_t1/i_t2) * (i_t2*j_t2)
	activity["off_chip_acc_num_meta"]["C_drain_IO_L3_out"] = C_drain_IO_L3_out_off_chip_acc_num
	off_chip_acc_num += C_drain_IO_L3_out_off_chip_acc_num
	activity["off_chip_acc_num"] = off_chip_acc_num

	noc_hop_num = 0
	A_IO_L2_in_io_noc_hop_num = (1 + (i_t1/i_t2)) / 2
	A_IO_L2_in_io_noc_hop_num *= ceil((i/i_t1)) * ceil((j/j_t1)) * ceil((k/k_t1)) * (((i_t1/i_t2) * (i_t2*k_t1)) + 0)
	noc_hop_num += A_IO_L2_in_io_noc_hop_num
	A_IO_L3_in_io_noc_hop_num = (1 + 1) / 2
	A_IO_L3_in_io_noc_hop_num *= ceil((i/i_t1)) * ceil((j/j_t1)) * ceil((k/k_t1)) * (i_t1/i_t2) * (i_t2*k_t1)
	noc_hop_num += A_IO_L3_in_io_noc_hop_num
	B_IO_L2_in_io_noc_hop_num = (1 + (j_t1/j_t2)) / 2
	B_IO_L2_in_io_noc_hop_num *= ceil((i/i_t1)) * ceil((j/j_t1)) * ceil((k/k_t1)) * (((j_t1/j_t2) * (j_t2*k_t1)) + 0)
	noc_hop_num += B_IO_L2_in_io_noc_hop_num
	B_IO_L3_in_io_noc_hop_num = (1 + 1) / 2
	B_IO_L3_in_io_noc_hop_num *= ceil((i/i_t1)) * ceil((j/j_t1)) * ceil((k/k_t1)) * (j_t1/j_t2) * (j_t2*k_t1)
	noc_hop_num += B_IO_L3_in_io_noc_hop_num
	C_drain_IO_L1_out_io_noc_hop_num = (1 + (i_t1/i_t2)) / 2
	C_drain_IO_L1_out_io_noc_hop_num *= ceil((i/i_t1)) * ceil((j/j_t1)) * ((i_t1/i_t2) * (i_t2*j_t2))
	C_drain_IO_L1_out_io_noc_hop_num *= (j_t1/j_t2)
	noc_hop_num += C_drain_IO_L1_out_io_noc_hop_num
	C_drain_IO_L2_out_io_noc_hop_num = (1 + (j_t1/j_t2)) / 2
	C_drain_IO_L2_out_io_noc_hop_num *= ceil((i/i_t1)) * ceil((j/j_t1)) * (j_t1/j_t2) * (i_t1/i_t2) * (i_t2*j_t2)
	noc_hop_num += C_drain_IO_L2_out_io_noc_hop_num
	C_drain_IO_L3_out_io_noc_hop_num = (1 + 1) / 2
	C_drain_IO_L3_out_io_noc_hop_num *= ceil((i/i_t1)) * ceil((j/j_t1)) * (j_t1/j_t2) * (i_t1/i_t2) * (i_t2*j_t2)
	noc_hop_num += C_drain_IO_L3_out_io_noc_hop_num
	A_IO_L2_in_pe_noc_hop_num = ((i_t1/i_t2)*(j_t1/j_t2))
	A_IO_L2_in_pe_noc_hop_num *= ceil((i/i_t1)) * ceil((j/j_t1)) * ceil((k/k_t1)) * (((k_t1/k_t2) * j_t2 * i_t2 * 1) + 0)
	A_IO_L2_in_pe_noc_hop_num *= k_t2
	noc_hop_num += A_IO_L2_in_pe_noc_hop_num
	B_IO_L2_in_pe_noc_hop_num = ((i_t1/i_t2)*(j_t1/j_t2))
	B_IO_L2_in_pe_noc_hop_num *= ceil((i/i_t1)) * ceil((j/j_t1)) * ceil((k/k_t1)) * (((k_t1/k_t2) * j_t2 * i_t2 * 1) + 0)
	B_IO_L2_in_pe_noc_hop_num *= k_t2
	noc_hop_num += B_IO_L2_in_pe_noc_hop_num
	C_drain_IO_L1_out_pe_noc_hop_num = ((i_t1/i_t2)*(j_t1/j_t2))
	C_drain_IO_L1_out_pe_noc_hop_num *= ceil((i/i_t1)) * ceil((j/j_t1)) * (j_t2 * i_t2 * 1)
	C_drain_IO_L1_out_pe_noc_hop_num *= 1
	noc_hop_num += C_drain_IO_L1_out_pe_noc_hop_num
	activity["noc_hop_num"] = noc_hop_num

	compute_stmt_call_num = 0
	compute_stmt_call_num = k_t2
	compute_stmt_call_num *= ceil((i/i_t1)) * ceil((j/j_t1)) * ceil((k/k_t1)) * (k_t1/k_t2) * j_t2 * i_t2 * 1
	compute_stmt_call_num *= ((i_t1/i_t2)*(j_t1/j_t2))
	activity["compute_stmt_call_num"] = compute_stmt_call_num

	io_module_mem_acc_num = 0
	A_IO_L2_in_mem_acc_num = ceil((i/i_t1)) * ceil((j/j_t1)) * ceil((k/k_t1)) * (((i_t1/i_t2) * (i_t2*k_t1/p9) + (k_t1/k_t2) * j_t2 * i_t2 * 1) + 0)
	A_IO_L2_in_mem_acc_num *= p9
	io_module_mem_acc_num += A_IO_L2_in_mem_acc_num
	B_IO_L2_in_mem_acc_num = ceil((i/i_t1)) * ceil((j/j_t1)) * ceil((k/k_t1)) * (((j_t1/j_t2) * (j_t2*k_t1/p10) + (k_t1/k_t2) * j_t2 * i_t2 * 1) + 0)
	B_IO_L2_in_mem_acc_num *= p10
	io_module_mem_acc_num += B_IO_L2_in_mem_acc_num
	C_drain_IO_L1_out_mem_acc_num = ceil((i/i_t1)) * ceil((j/j_t1)) * ((i_t1/i_t2) * (i_t2*j_t2/p12) + j_t2 * i_t2 * 1)
	C_drain_IO_L1_out_mem_acc_num *= p12
	io_module_mem_acc_num += C_drain_IO_L1_out_mem_acc_num
	activity["io_module_mem_acc_num"] = io_module_mem_acc_num

	pe_module_reg_acc_num = 0
	pe_module_mem_acc_num = 0
	pe_module_reg_acc_num = 2
	pe_module_mem_acc_num = 2
	pe_module_reg_acc_num *= k_t2
	pe_module_reg_acc_num *= ceil((i/i_t1)) * ceil((j/j_t1)) * ceil((k/k_t1)) * (k_t1/k_t2) * j_t2 * i_t2 * 1
	pe_module_reg_acc_num *= ((i_t1/i_t2)*(j_t1/j_t2))
	pe_module_mem_acc_num *= k_t2
	pe_module_mem_acc_num *= ceil((i/i_t1)) * ceil((j/j_t1)) * ceil((k/k_t1)) * (k_t1/k_t2) * j_t2 * i_t2 * 1
	pe_module_mem_acc_num *= ((i_t1/i_t2)*(j_t1/j_t2))
	activity["pe_module_reg_acc_num"] = pe_module_reg_acc_num
	activity["pe_module_mem_acc_num"] = pe_module_mem_acc_num

	return activity

def infer_params(params):
	i, j, k, i_t1, j_t1, k_t1, i_t2, j_t2, k_t2 = params["i"], params["j"], params["k"], params["i_t1"], params["j_t1"], params["k_t1"], params["i_t2"], params["j_t2"], params["k_t2"]

	p9_choices = [n*k_t2 for n in range(1, max(min(k_t1,16),k_t2)//k_t2+1) if max(min(k_t1,16),k_t2)%(n*k_t2)==0]
	if len(p9_choices) == 0:
		return None
	params["p9"] = max(p9_choices)
	p10_choices = [n*k_t2 for n in range(1, max(min(k_t1,16),k_t2)//k_t2+1) if max(min(k_t1,16),k_t2)%(n*k_t2)==0]
	if len(p10_choices) == 0:
		return None
	params["p10"] = max(p10_choices)
	p11_choices = [n*1 for n in range(1, max(min(j_t2,4),1)//1+1) if max(min(j_t2,4),1)%(n*1)==0]
	if len(p11_choices) == 0:
		return None
	params["p11"] = max(p11_choices)
	p12_choices = [n*1 for n in range(1, max(min(j_t2,4),1)//1+1) if max(min(j_t2,4),1)%(n*1)==0]
	if len(p12_choices) == 0:
		return None
	params["p12"] = max(p12_choices)

	return params

def random_sampling(params):
	def filter_non_power_of_two(x):
		if np.log2(x) != int(np.log2(x)):
			return True
		return False

	i = params["i"]
	j = params["j"]
	k = params["k"]
	while True:
		sample = random.randint(int(1), int(i))
		i_t1 = sample
		params["i_t1"] = sample
		sample = random.randint(int(1), int(k))
		k_t1 = sample
		params["k_t1"] = sample
		sample = random.randint(int(1), int(j))
		j_t1 = sample
		params["j_t1"] = sample
		sample = random.sample(utils.get_divisors(int(i_t1), None), 1)[-1]
		i_t2 = sample
		params["i_t2"] = sample
		sample = random.sample(utils.get_divisors(int(min(k_t1,8)), filter_non_power_of_two), 1)[-1]
		k_t2 = sample
		params["k_t2"] = sample
		sample = random.sample(utils.get_divisors(int(j_t1), None), 1)[-1]
		j_t2 = sample
		params["j_t2"] = sample
		latency_factors = 1
		latency_factors *= i_t2
		latency_factors *= j_t2
		simd_factor = k_t2
		if latency_factors >= 8 * simd_factor:
			break

	return params

def bound_check(params):
	def filter_non_power_of_two(x):
		if np.log2(x) != int(np.log2(x)):
			return True
		return False

	i, j, k, i_t1, j_t1, k_t1, i_t2, j_t2, k_t2, p9, p10, p11, p12 = params["i"], params["j"], params["k"], params["i_t1"], params["j_t1"], params["k_t1"], params["i_t2"], params["j_t2"], params["k_t2"], params["p9"], params["p10"], params["p11"], params["p12"]

	if i_t1 < 1:
		return False
	if j_t1 < 1:
		return False
	if k_t1 < 1:
		return False
	if i_t2 < 1:
		return False
	if i_t2 > i_t1:
		return False
	if j_t2 < 1:
		return False
	if j_t2 > j_t1:
		return False
	if k_t2 < 1:
		return False
	if k_t2 > min(k_t1,8):
		return False
	if filter_non_power_of_two(k_t2):
		return False
	if p9 < k_t2:
		return False
	if p9 > max(min(k_t1,16),k_t2):
		return False
	if filter_non_power_of_two(p9):
		return False
	if p10 < k_t2:
		return False
	if p10 > max(min(k_t1,16),k_t2):
		return False
	if filter_non_power_of_two(p10):
		return False
	if p11 < 1:
		return False
	if p11 > max(min(j_t2,4),1):
		return False
	if filter_non_power_of_two(p11):
		return False
	if p12 < 1:
		return False
	if p12 > max(min(j_t2,4),1):
		return False
	if filter_non_power_of_two(p12):
		return False
	latency_factors = 1
	latency_factors *= i_t2
	latency_factors *= j_t2
	simd_factor = k_t2
	if latency_factors < 8 * simd_factor:
		return False
	return True

def compute_arch_cst(params):
	i, j, k, i_t1, j_t1, k_t1, i_t2, j_t2, k_t2, p9, p10, p11, p12 = params["i"], params["j"], params["k"], params["i_t1"], params["j_t1"], params["k_t1"], params["i_t2"], params["j_t2"], params["k_t2"], params["p9"], params["p10"], params["p11"], params["p12"]

	arch_features = {}
	arch_features['dims'] = []
	arch_features["dims"].append((i_t1/i_t2))
	if arch_features["dims"][-1] == 0:
		return None
	arch_features["dims"].append((j_t1/j_t2))
	if arch_features["dims"][-1] == 0:
		return None
	arch_features["SIMD"] = k_t2
	arch_features["data_pack"] = {}

	return arch_features

