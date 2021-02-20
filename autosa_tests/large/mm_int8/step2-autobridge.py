#! /usr/bin/python3.6

# add the path to where you place the autobridge source code
import sys
sys.path.append('../src')

import graph
from formator import FormatHLS
import collections
import os
import subprocess

"""
AutoBridge divides the target device as follows and assign each HLS function to one slot
For more details pls refer to the paper

      u250                     u280
   -----------
 3 |    |    |
   |----|----|              |----|----|
 2 |    |    |            2 |    |    |
   |----|----|              |----|----|
 1 |    |    |            1 |    |    |
   |----|----|              |----|----|
 0 |    |    |            0 |    |    |
   -----------              -----------
     0    1                   0    1
"""

################### Modify Accordingly ###############################

# (1) fill basic information
project_path = '/home/jaywang/doc_examples/mm_int8_ab_pe/kernel0' # path to your hls project
#project_path = '/home/jaywang/doc_examples/mm_ab/kernel0' # path to your hls project
top_name = 'kernel0' # name of the top function in your hls design
solution_path = f'{project_path}/solution/'
project_name = 'kernel0'
board_name = 'u250' # or 'u280'
# where the results will be saved. Your HLS project will be copied there and your top RTL will be replaced.
# Note that if the directory already exists, we will try to reset the contents

# (2) specify how your designs connect to the external memory
""" Example:

void kernel0(ap_uint<512> *p1, ap_uint<512> *p2)
{
  #pragma HLS INTERFACE m_axi port=p1 offset=slave bundle=gmem_A
  #pragma HLS INTERFACE m_axi port=p2 offset=slave bundle=gmem_B

  load_p1 (p1, ...);
  load_p2 (p2, ...);
}

--------------------------------------

In this example, the pointer p1 and p2 will become M_AXI controllers to connect to the dedicated DDR IP.
If you want p1 to connect to DDR 2 in the 2-nd SLR, then you need to specify that the corresponding RTL controller must be floorplanned at the 2-nd SLR
Meanwhile, your function load_p1() will talk to the M_AXI controller also through AXI interface which cannot be easily pipelined.
Thus the RTL module corresponds to load_p1() must also be in the 2-nd SLR in this example.
Since load_p1() will communicate with the rest of your design using FIFO interface, you don't need to specify the location of other modules

(transparent)|                        (user visible)
             |
   Vitis     |                    what your HLS design becomes
             |
             | M_AXI                     AXI                        FIFO
DDR IP  <--- | ----> M_AXI controller <-------> your first module <-------> your other modules
(fixed loc)  |         (p1)                       (load_p1)
             |
             | M_AXI                     AXI                        FIFO
DDR IP  <--- | ----> M_AXI controller <-------> your first module <-------> your other modules
(fixed loc)  |         (p2)                       (load_p2)
             |
             | S_AXI
PCIe    <--- | ----> S_AXI controller
             |
"""

# on the left side or the right side of an SLR
DDR_loc_2d_x = collections.defaultdict(dict)

# on which SLR
DDR_loc_2d_y = collections.defaultdict(dict)

# use DDR 0, 1, 3
DDR_loc_2d_y['A_IO_L3_in_serialize_U0'] = 0
DDR_loc_2d_x['A_IO_L3_in_serialize_U0'] = 0
DDR_loc_2d_y['kernel0_gmem_A_m_axi_U'] = 0
DDR_loc_2d_x['kernel0_gmem_A_m_axi_U'] = 0

DDR_loc_2d_y['B_IO_L3_in_serialize_U0'] = 1
DDR_loc_2d_x['B_IO_L3_in_serialize_U0'] = 0
DDR_loc_2d_y['kernel0_gmem_B_m_axi_U'] = 1
DDR_loc_2d_x['kernel0_gmem_B_m_axi_U'] = 0

DDR_loc_2d_y['C_drain_IO_L3_out_serialize_U0'] = 3
DDR_loc_2d_x['C_drain_IO_L3_out_serialize_U0'] = 0
DDR_loc_2d_y['kernel0_gmem_C_m_axi_U'] = 3
DDR_loc_2d_x['kernel0_gmem_C_m_axi_U'] = 0

DDR_loc_2d_y['kernel0_control_s_axi_U'] = 1
DDR_loc_2d_x['kernel0_control_s_axi_U'] = 1
DDR_loc_2d_y['kernel0_entry12_U0'] = 1
DDR_loc_2d_x['kernel0_entry12_U0'] = 1

# (3) specify DDR information
# If you instantiate a DDR controller, it will consume non-trivial amount of resource
# to make the floorplanning better, you need to specify which DDRs have been enabled
# In this example, you connect p1 to DDR-2 in SLR-2 and p2 to DDR-1 in SLR-1
# If you want to use all DDRs, for example, you need to set it as [1, 1, 1, 1]
DDR_enable = [1, 1, 0, 1]

# (4) specify how much resource can be used in each slot
# In this way you could force the design to be placed evenly across the device and avoid local congestion
""" Example:
   -----------
 3 |0.76|0.62|
   |----|----|
 2 |0.74|0.61|
   |----|----|
 1 |0.75|0.6 |
   |----|----|
 0 | 0.7|0.6 |
   -----------
     0    1
"""
max_usage_ratio_2d = [ [0.8, 0.6], [0.8, 0.6], [0.8, 0.8], [0.8, 0.6] ]


##################### DON'T TOUCH THE SECTION BELOW #################################
target_dir = '/home/jaywang/doc_examples/mm_int8_ab_pe/autobridge'

formator = FormatHLS(
  rpt_path = f'{solution_path}/syn/report/',
  hls_sche_path = f'{solution_path}/.autopilot/db/',
  top_hdl_path = f'{solution_path}/syn/verilog/{top_name}_{top_name}.v',
  top_name = top_name,
  DDR_loc_2d_x = DDR_loc_2d_x,
  DDR_loc_2d_y = DDR_loc_2d_y,
  DDR_enable = DDR_enable,
  max_usage_ratio_2d = max_usage_ratio_2d,
  board_name = board_name,
  target_dir = target_dir,
  relay_station_count = lambda x : 2 * x, # how many levels of relay stations to add for x-unit of crossing
  max_search_time = 600,
  NaiveBalance = True)

# run floorplanning
g = graph.Graph(formator)

# move results to target dir
if (os.path.isdir(target_dir)):
  subprocess.run(['rm', '-rf', f'{target_dir}'])
subprocess.run(['mkdir', f'{target_dir}/'])
subprocess.run(['cp', '-r', project_path, f'{target_dir}/{project_name}'])
subprocess.run(['cp', os.path.realpath(__file__), f'{target_dir}/archived_source.txt'])
subprocess.run(['chmod', '+w', '-R', f'{target_dir}'])
subprocess.run(['cp', 'constraint.tcl', target_dir])
subprocess.run(['cp', 'pack_xo.tcl', target_dir])
subprocess.run(['cp', 'autobridge.log', target_dir])
subprocess.run(['cp', f'{top_name}_{top_name}.v', f'{target_dir}/{project_name}/solution/syn/verilog/'])

# clean up
os.system('rm *.lp')
subprocess.run(['rm', 'parser.out'])
subprocess.run(['rm', 'parsetab.py'])
subprocess.run(['rm', '-rf', '__pycache__'])

