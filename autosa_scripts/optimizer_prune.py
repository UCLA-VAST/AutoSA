#!/usr/bin/env python3

def array_part_loops_pruning(loops, config):
    """ Apply pruning on array partitioning candidate loops.

    At present, we apply the following heuristics:
    - The product of all array_part loops should be greater than the total PE number  
    - TODO: Prune based on off-chip traffic

    Parameters
    ----------
    loops: list
        A list of candidate loops
    config:
        Global configuration
    """
    pruned_loops = []

    PE_lb = config['setting'][config['mode']
                              ]['pruning']['array_part']['PE_num'][0]
    for loop in loops:
        if PE_lb == -1:
            pruned_loops.append(loop)
        else:
            prod = 1
            for l in loop:
                if l > 1:
                    prod *= l
            if prod < PE_lb:
                continue
            pruned_loops.append(loop)

    return pruned_loops


def array_part_L2_loops_pruning(loops, config):
    """ Apply pruning on L2 array partitioning candidate loops.

    At present, we wpply the following heuristics:
    - We only apply L2 array partitioning on parallel loops to save off-chip communication.
      We examine from outer loops to inner loops. Once we meet a non-parallel loop,
      we will stop from here, and set the tiling factors from here to below to maximum.

    Parameters
    ----------
    loops: list
        A list of candidate loops
    config:
        Global configuration  
    """
    pruned_loops = []
    tuning = config['tuning']
    loop_stop = 0
    for c in tuning['array_part_L2']['coincident']:
        if not c:
            break
        loop_stop += 1
    ubs = tuning['array_part_L2']['tilable_loops'][loop_stop:]
    for loop in loops:
        # Examine [loop_stop:-1], only leave those that equal the upper bound
        loop_cut = loop[loop_stop:]
        if loop_cut != ubs:
            continue
        pruned_loops.append(loop)

    return pruned_loops


def latency_hiding_loops_pruning(loops, config):
    """ Apply pruning on latency hiding candidate loops.

    At present, we apply the following heuristics:
    - We compute the latency hiding register sizes and prune it when it is 
      greater or less than the pre-set threshold.

    Parameters
    ----------
    loops: list
        A list of candidate loops
    config:
        Global configuration
    """
    pruned_loops = []
    reg_size_lb = config['setting'][config['mode']
                                    ]['pruning']['latency_hiding']['reg_size'][0]
    reg_size_ub = config['setting'][config['mode']
                                    ]['pruning']['latency_hiding']['reg_size'][1]
    for loop in loops:
        size = 1
        for l in loop:
            size *= l
        if reg_size_lb != -1:
            if size < reg_size_lb:
                continue
        if reg_size_ub != -1:
            if size > reg_size_ub:
                continue
        pruned_loops.append(loop)

    return pruned_loops


def SIMD_vectorization_PE_pruning(config, postpone=0):
    """ Apply pruning based on the PE structures at the SIMD vectorization stage.

    At present, we apply the following heuristics:
    - We restrain the PE number within certain range
    - We restrain the PE shape for 2D array

    Parameters
    ----------
    config: dict
        Global configuration
    postpone: int
        If the pruning is postponed after the SIMD optimization

    Returns
    -------
    ret: boolean
        If this configuration is to be pruned.
    """
    tuning = config['tuning']
    ret = False
    PE_num_lb = config['setting'][config['mode']
                                  ]['pruning']['SIMD_vectorization']['PE_num'][0]
    PE_num_ub = config['setting'][config['mode']
                                  ]['pruning']['SIMD_vectorization']['PE_num'][1]
    if postpone == 0:
        sa_dims = tuning['simd']['sa_dims']
    else:
        sa_dims = tuning['sa_dims']

    n_pe = 1
    for dim in sa_dims:
        n_pe *= int(dim)
    if PE_num_lb != -1:
        if n_pe < PE_num_lb:
            return True
    if PE_num_ub != -1:
        if n_pe > PE_num_ub:
            return True
    
    if len(sa_dims) > 1:
        sa_dims.sort(reverse=True)
        pe_ratio = sa_dims[0] / sa_dims[1]
        if config['setting'][config['mode']]['pruning']['SIMD_vectorization']['PE_ratio'] != -1:
            if pe_ratio > config['setting'][config['mode']]['pruning']['SIMD_vectorization']['PE_ratio']:
                return True

    return ret


def reorder_simd_loops(loops):
    """ Reorder the simd loops for pruning.

    The input loops contains a list of candidate loops. 
    For each candidate loop, it is in the format of [1, 1, X].
    We will sort the loops based on the non-one element in descending order.    

    Parameters
    ----------
    loops: list
        A list containing all candidate SIMD loops to be evaluated.
    """
    # Find the position of the non-one element.
    pos = -1
    for loop in loops:
        for i in range(len(loop)):
            if loop[i] != 1:
                pos = i
                break
        if pos != -1:
            break

    if pos == -1:
        # All the loops are ones.
        return loops

    loops.sort(key=lambda x: x[pos], reverse=True)
    return loops


def SIMD_vectorization_latency_pruning(config):
    """ Perform latency-based pruning at the SIMD vectorization stage.

    We have already reordered the SIMD candidate loops in descending order.
    Therefore, if the last design evaluated is slower than the opt design found
    so far, there is no chance for the rest of candidates which has a smaller 
    SIMD factor to beat the opt design. 
    We will stop exploration for these loops and return.
    Otherwise, if the resource usage is legal, we have already found a design that 
    achieves the least latency in the current group. For the other designs with 
    a smaller SIMD factor, their latency is no less than the current design.
    We will stop exploration for these loops and return.
    However, there a chance that the designs with a smaller SIMD factor acheives 
    the same latency but with less resource usage (for a comm bound design). 
    At present, we ignore such cases.

    """
    last_design = config['monitor']['last_design']
    if last_design['latency'] == -1:
        # The current design is already slower than opt., stop exploration.
        return True
    else:
        # The current design is resource-legal, stop exploration.
        if not last_design['resource']:
            return True
    return False
