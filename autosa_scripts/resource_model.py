import os
import json
import re
import xml.etree.ElementTree as ET
import numpy as np
import pandas as pd
import joblib
from sklearn.linear_model import LinearRegression
from sklearn import metrics
from sklearn.model_selection import train_test_split
from scipy.stats.mstats import gmean
from statistics import mean
import shutil
import math
import pprint
import argparse

# Helper functions to predict certain modules
def BRAM_predict_HLS(dw, depth, use_18K=0):
    """ Predict the resource usage of BRAM on Xilinx platforms.  

    Parameters
    ----------
    dw: int
        BRAM port width
    depth: int
        BRAM depth
    use_18K: int
        Force the estimator to use the BRAM18K model. (for HLS FIFOs)
    """
    if dw <= 18 or use_18K:
        alpha = np.ceil(float(dw) / 18)
        BRAM = alpha * np.ceil(float(depth) / 1024)   
    else:
        alpha = np.ceil(float(dw) / 36)
        BRAM = alpha * np.ceil(float(depth) / 512)    
        
    return BRAM

def URAM_predict_HLS(dw, depth):
    """ Predict the resource usage of URAM on Xilinx platforms.  

    Parameters
    ----------
    dw: int
        URAM port width
    depth: int
        URAM depth
    """
    alpha = np.ceil(float(dw) / 72)
    URAM = alpha * np.ceil(float(depth) / 4096)
    return URAM

def BRAM_array_predict_HLS(dw, depth, n_part):
    """ Predict the BRAM resource usage of arrays on Xilinx platform.  

    Parameters
    ----------
    dw: int
        BRAM port width (in bytes)
    depth: int
        BRAM depth
    n_part: int
        number of partitions
    """
    return n_part * BRAM_predict_HLS(dw * 8, np.ceil(float(depth) / n_part))

def FF_array_predict_HLS(dw, depth):
    """ Predict the FF resource usage of arrays on Xilinx platform.

    Parameters
    ----------
    dw: int
        BRAM port width (in bytes)
    depth : int
        BRAM depth
    """
    return dw * 8 * depth

def URAM_array_predict_HLS(dw, depth, n_part):
    return n_part * URAM_predict_HLS(dw * 8, np.ceil(float(depth) / n_part))

def FIFO_predict_xilinx(dw, depth):
    """ Predict the resource ussage of fifo modules on Xilinx platforms.
  

    Parameters
    ----------
    dw: int
        fifo data width
    depth: int
        fifo depth
    """
    DSP = 0
    if dw * depth <= 512:
        BRAM = 0
        FF = 5
        LUT = dw + 12
    else:
        BRAM = BRAM_predict_HLS(dw, depth, 1)        
    # In the current codegen, we will use SRL to implement FIFOs
    #    BRAM = 0
        FF = dw + 10
        LUT = int(0.9687 * dw + 13.982)

    return {'BRAM18K': BRAM, 'DSP': DSP, 'FF': FF, 'LUT': LUT}

def extract_axi_res_from_hls_rpt(rpt_path):
    """ Extract the resource usage for AXI modules from the HLS report in text format

    Parameters
    ----------
    rpt_path: str
        The path of HLS report

    Returns
    -------
    BRAM18K, FF, LUT
    """
    with open(rpt_path) as f:
        lines = f.readlines()
    BRAM18K_total = 0
    FF_total = 0
    LUT_total = 0
    for line in lines:
        if line.find('kernel0_gmem_') != -1:
            line = line.split('|')
            BRAM18K_total += float(line[3])
            FF_total += float(line[5])
            LUT_total += float(line[6])
    return BRAM18K_total, FF_total, LUT_total

def extract_design_info(design_dir, synth=0):
    """ Extract the design infomation.

    Load the design_info.json and design_info.dat under the diretory 'resource_est'.
    If synth is set to 1, load the HLS reports.
    Return a dictionary that contains all the information above.
    - FF: int
    - LUT: int
    - BRAM18K: int
    - DSP: int
    - URAM: int
    - fifos:
      - fifo_name:
        - fifo_cnt: int
        - fifo_width: int
        - fifo_depth: int
    - modules:
      - module_name:
        - module_cnt: int
        - FF, LUT, BRAM18K, URAM, DSP: int
        - data_pack_inter, data_pack_intra: int
        - ele_type: str
        - ele_size: int
        - local_buffers
        - unroll: int

    Parameters
    ----------
    design_dir: str
        The design directory.
    synth: int
        Is the design synthesized or not.
    """
    # Load the design info
    f_dir = f'{design_dir}/resource_est/design_info.json'
    with open(f_dir, 'r') as f:
        design_info = json.load(f)
    design_info['fifos'] = {}
    f_dir = f'{design_dir}/resource_est/design_info.dat'
    with open(f_dir, 'r') as f:
        lines = f.readlines()
    for line in lines:
        line = line.strip().split(':')
        if line[0] == 'fifo':
            fifo_name = line[1]
            fifo_cnt = int(line[2])
            fifo_w = int(line[3])
            fifo_depth = 2 # default                 
            design_info['fifos'][fifo_name] = {
                'fifo_cnt': fifo_cnt,
                'fifo_width': fifo_w,
                'fifo_depth': fifo_depth
            }
            if fifo_cnt == 0 and fifo_name in design_info['fifos']:
                design_info['fifos'].pop(fifo_name)
        elif line[0] == 'module':
            module_name = line[1]
            module_cnt = int(line[2])                        
            design_info['modules'][module_name]['module_cnt'] = module_cnt
            if module_cnt == 0 and module_name in design_info['modules']:
                design_info['modules'].pop(module_name)
    if synth:
        # Load the HLS project              
        hls_rpts = {}
        hls_prj_dir = f'{design_dir}/hls_prj'
        hls_rpts_dir = f'{hls_prj_dir}/solution1/syn/report'
        hls_rpt_names = os.listdir(hls_rpts_dir)
        hls_rpt_names = [r for r in hls_rpt_names if r.endswith('_csynth.xml')]
        for r in hls_rpt_names:
            with open(hls_rpts_dir + '/' + r, 'r') as f:
                tree = ET.parse(f)
                root = tree.getroot()
                module_name = r[:-11]
                # For duplicate modules, get rid of the digits suffix.
                while module_name[-1].isdigit():
                    module_name = module_name[:-1]
                hls_rpts[module_name] = root
        
        # Extract the resource info from the hls report
        for module in design_info['modules']:
            if module in hls_rpts:
                rpt = hls_rpts[module]
            elif f'{module}_wrapper' in hls_rpts:
                # It is possible the module is wrapped. 
                # Look for the wrapper module.
                rpt = hls_rpts[module + '_wrapper']
            else:
                # The module is inlined
                rpt = None

            if rpt:
                res = extract_resource_info_from_hls_rpt(rpt)
                design_info['modules'][module]['FF'] = res['FF']
                # Extract the FF storage if existing
                if "local_buffers" in design_info['modules'][module]:
                    local_buffers = design_info['modules'][module]['local_buffers']
                    for local_buffer in local_buffers:
                        if local_buffer['mem_type'] == 'FF':
                            design_info['modules'][module]['FF'] -= \
                                FF_array_predict_HLS(local_buffer['port_width'], \
                                                     local_buffer['buffer_depth'])                            
                design_info['modules'][module]['LUT'] = res['LUT']
                design_info['modules'][module]['BRAM18K'] = res['BRAM18K']
                design_info['modules'][module]['URAM'] = res['URAM']
                design_info['modules'][module]['DSP'] = res['DSP']
            else:
                # For inlined module, its resource usage is included in the parent module.
                design_info['modules'][module]['FF'] = None
                design_info['modules'][module]['LUT'] = None
                design_info['modules'][module]['BRAM18K'] = None
                design_info['modules'][module]['URAM'] = None
                design_info['modules'][module]['DSP'] = None                
        # Top module
        rpt = hls_rpts['kernel']
        res = extract_resource_info_from_hls_rpt(rpt) 
        # For the top module, we will also parse the report for BRAM usage of AXI modules
        top_module_rpt_name = 'kernel0_csynth.rpt'
        axi_bram, axi_ff, axi_lut = extract_axi_res_from_hls_rpt(f'{hls_rpts_dir}/{top_module_rpt_name}')
        res['BRAM18K'] -= axi_bram
        res['FF'] -= axi_ff
        res['LUT'] -= axi_lut

        design_info['FF'] = res['FF']
        design_info['LUT'] = res['LUT']
        design_info['BRAM18K'] = res['BRAM18K']
        design_info['URAM'] = res['URAM']
        design_info['DSP'] = res['DSP']
    else:
        for module in design_info['modules']:
            design_info['modules'][module]['FF'] = None
            design_info['modules'][module]['LUT'] = None
            design_info['modules'][module]['BRAM18K'] = None
            design_info['modules'][module]['URAM'] = None
            design_info['modules'][module]['DSP'] = None
        design_info['FF'] = None
        design_info['LUT'] = None
        design_info['BRAM18K'] = None
        design_info['URAM'] = None
        design_info['DSP'] = None

    return design_info

def extract_resource_info_from_hls_rpt(rpt):
    """ Extract the resource info from the HLS rpt.

    Parameters
    ----------
    rpt: 
        HLS report in XML format
    """
    res = {
        'BRAM18K': 0,
        'DSP': 0,
        'URAM': 0,
        'FF': 0,
        'LUT': 0
    }
    root = rpt
    for est in root.iter('AreaEstimates'):
        for child in est:
            if child.tag == 'Resources':
                for item in child:
                    if item.tag == 'BRAM_18K':
                        res['BRAM18K'] = int(item.text)
                    elif item.tag == 'URAM':
                        res['URAM'] = int(item.text)
                    elif item.tag == 'DSP48E':
                        res['DSP'] = int(item.text)    
                    elif item.tag == 'FF':
                        res['FF'] = int(item.text)   
                    elif item.tag == 'LUT':
                        res['LUT'] = int(item.text)                        

    return res

def convert_design_infos_to_df(design_infos):
    """ Convert the design infos into a dataframe.

    Parameters
    ----------
    design_infos: list
        A list containing all design informations.
    """
    modules = []
    fifos = []
    for design_info in design_infos:
        fs = design_info['fifos']
        ms = design_info['modules']
        for f in fs:
            if f not in fifos:
                fifos.append(f)
        for m in ms:
            if m not in modules and m.find('wrapper') == -1:
                modules.append(m)

    # Reorganize the design information to a dictionary
    info_dict = {}
    info_dict['FF'] = []
    info_dict['LUT'] = []
    info_dict['DSP'] = []
    info_dict['BRAM18K'] = []
    info_dict['URAM'] = []
    for fifo in fifos:
        info_dict[fifo + '_fifo_cnt'] = []
        info_dict[fifo + '_fifo_width'] = []
        info_dict[fifo + '_fifo_depth'] = []
    for module in modules:
        # IO_module: 
        #   module_cnt, data_pack_inter, data_pack_intra, ele_type, ele_size
        #   [local_buffers_local_X]_{port_width, buffer_depth, partition_number}
        # PE_module: 
        #   module_cnt, unroll
        if module.find('IO') != -1:
            # IO module
            info_dict[module + '_data_pack_inter'] = []
            info_dict[module + '_data_pack_intra'] = []
            info_dict[module + '_ele_size'] = []
        else:
            # PE module
            info_dict[module + '_unroll'] = []
        
        info_dict[module + '_module_cnt'] = []
        info_dict[module + '_FF'] = []
        info_dict[module + '_LUT'] = []
        info_dict[module + '_BRAM18K'] = []
        info_dict[module + '_URAM'] = []
        info_dict[module + '_DSP'] = []

    for design_info in design_infos:
        # FF, LUT, BRAM, DSP
        info_dict['FF'].append(design_info['FF'])
        info_dict['LUT'].append(design_info['LUT'])
        info_dict['DSP'].append(design_info['DSP'])
        info_dict['BRAM18K'].append(design_info['BRAM18K'])
        info_dict['URAM'].append(design_info['URAM'])

        fs = design_info['fifos']
        ms = design_info['modules']
        for fifo in fifos:
            if fifo in fs:
                info_dict[fifo + '_fifo_cnt'].append(fs[fifo]['fifo_cnt'])
                info_dict[fifo + '_fifo_width'].append(fs[fifo]['fifo_width'])
                info_dict[fifo + '_fifo_depth'].append(fs[fifo]['fifo_depth'])
            else:
                info_dict[fifo + '_fifo_cnt'].append(None)
                info_dict[fifo + '_fifo_width'].append(None)
                info_dict[fifo + '_fifo_depth'].append(None)
    
        for module in modules:
            if module.find('IO') != -1:
                # IO module
                if module in ms:
                    info_dict[module + '_module_cnt'].append(ms[module]['module_cnt'])
                    info_dict[module + '_data_pack_inter'].append(ms[module]['data_pack_inter'])
                    info_dict[module + '_data_pack_intra'].append(ms[module]['data_pack_intra'])
                    info_dict[module + '_ele_size'].append(ms[module]['ele_size'])
                else:
                    info_dict[module + '_module_cnt'].append(None)
                    info_dict[module + '_data_pack_inter'].append(None)
                    info_dict[module + '_data_pack_intra'].append(None)
                    info_dict[module + '_ele_size'].append(None)
            else:
                # PE module
                if module in ms:
                    info_dict[module + '_module_cnt'].append(ms[module]['module_cnt'])
                    info_dict[module + '_unroll'].append(ms[module]['unroll'])
                else:
                    info_dict[module + '_module_cnt'].append(None)
                    info_dict[module + '_unroll'].append(None)
      
            if module in ms:
                info_dict[module + '_FF'].append(ms[module]['FF'])
                info_dict[module + '_LUT'].append(ms[module]['LUT'])
                info_dict[module + '_BRAM18K'].append(ms[module]['BRAM18K'])
                info_dict[module + '_URAM'].append(ms[module]['URAM'])
                info_dict[module + '_DSP'].append(ms[module]['DSP'])
            else:
                info_dict[module + '_FF'].append(None)
                info_dict[module + '_LUT'].append(None)
                info_dict[module + '_BRAM18K'].append(None)
                info_dict[module + '_URAM'].append(None)
                info_dict[module + '_DSP'].append(None)

    df = pd.DataFrame(info_dict)
    return modules, fifos, df 

def df_feature_extract(df, module):
    """ Expand the dataframe to include new features for the module.

    Parameters
    ----------
    df: dataframe
    module: str
    """
    if module.find('IO') != -1:
        df[module + '_data_pack_inter/' + module + '_data_pack_intra'] = \
            df.apply(lambda row: float(row[module + '_data_pack_inter']) / float(row[module + '_data_pack_intra']), axis = 1)
        #df[module + '_data_pack_inter*' + module + '_ele_size'] = \
        #    df.apply(lambda row: float(row[module + '_data_pack_inter']) * float(row[module + '_ele_size']), axis = 1)

    return df

def get_feature_set(module):
    """ Exatract the feature set for the resource models.

    Parameters
    ----------
    module: str
        Module name.
    """
    feature_set = []
    if 'IO' in module:
        feature_set.append(f'{module}_data_pack_inter')
        feature_set.append(f'{module}_data_pack_inter/{module}_data_pack_intra')
    else:
        feature_set.append(f'{module}_unroll')
    return feature_set

def train(df, modules, fifos, design_infos, work_dir, logger):
    """ Train the resource models for each module.

    Parameters
    ----------
    df: dataframe
        A dataframe that containing all designs
    modules: list
        Module name list.
    fifos: list
        FIFO name list.
    design_infos: list
        A list containing all design informations.
    work_dir: str
        Directory to save the trained models.
    logger:
        Logger.
    """
    # Split the training set and validation set.
    feature_set = []
    pred_set = []
    for module in modules:
        # Expand the dataframe if necessary        
        df = df_feature_extract(df, module)
        feature_set += get_feature_set(module)        
        pred_set.append(module + '_FF')
        pred_set.append(module + '_LUT')
        pred_set.append(module + '_BRAM18K')
        pred_set.append(module + '_URAM')
        pred_set.append(module + '_DSP')

    X = df.loc[:, feature_set]
    y = df.loc[:, pred_set]
    X_train, X_test, y_train, y_test = train_test_split(X, y, test_size=0.2, random_state=0)
    logger.info(f'#Training samples: {X_train.shape[0]}')
    logger.info(f'#Validation samples: {X_test.shape[0]}')

    # Evaluation metrics
    FF_mape = []
    LUT_mape = []
    DSP_mape = []
    BRAM18K_mape = []
    URAM_mape = []    
    
    for module in modules:
        logger.info('Training resource model for module: ' + module)
        feature_set = get_feature_set(module)

        # FF
        pred_set = [module + '_FF']
        y_train_module = y_train.loc[:, pred_set]        
        y_train_module = y_train_module.dropna()        
        X_train_module = X_train.loc[y_train_module.index, feature_set]                
        if X_train_module.shape[0] > 0:
            model = LinearRegression()
            model.fit(X_train_module.to_numpy(), y_train_module.to_numpy())
            model_name = module + '_FF_model'
            joblib_file = work_dir + '/' + model_name + '.pkl'
            joblib.dump(model, joblib_file)
        # Validate the accuracy
        y_test_module = y_test.loc[:, pred_set]
        y_test_module = y_test_module.dropna()
        X_test_module = X_test.loc[y_test_module.index, feature_set]        
        if X_test_module.shape[0] > 0:
            y_pred_module = model.predict(X_test_module.to_numpy())        
            y_test_module = y_test_module.to_numpy()
            logger.info('======== FF ========')
            logger.info(f'Mean Absolute Error: {metrics.mean_absolute_error(y_test_module, y_pred_module)}')
            logger.info(f'Mean Squared Error: {metrics.mean_squared_error(y_test_module, y_pred_module)}')
            logger.info(f'Mean Absolute Percentage Error: {mean_absolute_percentage_error(y_test_module, y_pred_module)}')
            FF_mape.append(mean_absolute_percentage_error(y_test_module, y_pred_module))

        # LUT
        pred_set = [module + '_LUT']
        y_train_module = y_train.loc[:, pred_set]
        y_train_module = y_train_module.dropna()
        X_train_module = X_train.loc[y_train_module.index, feature_set]        
        if X_train_module.shape[0] > 0:
            model = LinearRegression()
            model.fit(X_train_module.to_numpy(), y_train_module.to_numpy())
            model_name = module + '_LUT_model'
            joblib_file = work_dir + '/' + model_name + '.pkl'
            joblib.dump(model, joblib_file)
        # Validate the accuracy
        y_test_module = y_test.loc[:, pred_set]
        y_test_module = y_test_module.dropna()
        X_test_module = X_test.loc[y_test_module.index, feature_set]        
        if X_test_module.shape[0] > 0:
            y_pred_module = model.predict(X_test_module.to_numpy())        
            y_test_module = y_test_module.to_numpy()
            logger.info('======== LUT ========')
            logger.info(f'Mean Absolute Error: {metrics.mean_absolute_error(y_test_module, y_pred_module)}')
            logger.info(f'Mean Squared Error: {metrics.mean_squared_error(y_test_module, y_pred_module)}')
            logger.info(f'Mean Absolute Percentage Error: {mean_absolute_percentage_error(y_test_module, y_pred_module)}')
            LUT_mape.append(mean_absolute_percentage_error(y_test_module, y_pred_module))

        # DSP
        pred_set = [module + '_DSP']
        y_train_module = y_train.loc[:, pred_set]
        y_train_module = y_train_module.dropna()
        X_train_module = X_train.loc[y_train_module.index, feature_set]
        if X_train_module.shape[0] > 0:
            model = LinearRegression()
            model.fit(X_train_module.to_numpy(), y_train_module.to_numpy())
            model_name = module + '_DSP_model'
            joblib_file = work_dir + '/' + model_name + '.pkl'
            joblib.dump(model, joblib_file)
        # Validate the accuracy
        y_test_module = y_test.loc[:, pred_set]
        y_test_module = y_test_module.dropna()
        X_test_module = X_test.loc[y_test_module.index, feature_set]        
        if X_test_module.shape[0] > 0:
            y_pred_module = model.predict(X_test_module.to_numpy())        
            y_test_module = y_test_module.to_numpy()        
            logger.info('======== DSP ========')
            logger.info(f'Mean Absolute Error: {metrics.mean_absolute_error(y_test_module, y_pred_module)}')
            logger.info(f'Mean Squared Error: {metrics.mean_squared_error(y_test_module, y_pred_module)}')
            logger.info(f'Mean Absolute Percentage Error: {mean_absolute_percentage_error(y_test_module, y_pred_module)}')
            DSP_mape.append(mean_absolute_percentage_error(y_test_module, y_pred_module))

        # BRAM18K
        pred_set = [module + '_BRAM18K']
        y_test_module = y_test.loc[:, pred_set]        
        y_test_module = y_test_module.dropna()
        X_test_module = X_test.loc[y_test_module.index, feature_set]        
        if X_test_module.shape[0] > 0:
            y_pred_module = np.zeros((y_test_module.shape[0], 1), dtype=float)
            cnt = 0
            for index, row in y_test_module.iterrows():            
                design_info = design_infos[index]
                BRAM_usage = 0
                if "local_buffers" in design_info['modules'][module]:
                    local_buffers = design_info['modules'][module]['local_buffers']
                    for local_buffer in local_buffers:
                        if local_buffer['mem_type'] == 'BRAM':
                            if 'array_map' in local_buffer:
                                # For horizontal mapping, we will merge two ping/pong buffers to one
                                BRAM_usage += BRAM_array_predict_HLS(local_buffer['port_width'], \
                                    local_buffer['buffer_depth'] * 2, local_buffer['partition_number']) / 2
                            else:
                                BRAM_usage += BRAM_array_predict_HLS(local_buffer['port_width'], \
                                    local_buffer['buffer_depth'], local_buffer['partition_number'])                                  

                y_pred_module[cnt] = BRAM_usage
                cnt += 1

            y_test_module = y_test_module.to_numpy()
            logger.info('======== BRAM18K ========')
            logger.info(f'Mean Absolute Error: {metrics.mean_absolute_error(y_test_module, y_pred_module)}')
            logger.info(f'Mean Squared Error: {metrics.mean_squared_error(y_test_module, y_pred_module)}')
            logger.info(f'Mean Absolute Percentage Error: {mean_absolute_percentage_error(y_test_module, y_pred_module)}')
            BRAM18K_mape.append(mean_absolute_percentage_error(y_test_module, y_pred_module))

        # URAM
        pred_set = [module + '_URAM']
        y_test_module = y_test.loc[:, pred_set]        
        y_test_module = y_test_module.dropna()
        X_test_module = X_test.loc[y_test_module.index, feature_set]     
        if X_test_module.shape[0] > 0:           
            y_pred_module = np.zeros((y_test_module.shape[0], 1), dtype=float)
            cnt = 0
            for index, row in y_test_module.iterrows():
                design = 'design' + str(index)
                design_info = design_infos[index]
                URAM_usage = 0
                if "local_buffers" in design_info['modules'][module]:
                    local_buffers = design_info['modules'][module]['local_buffers']
                    for local_buffer in local_buffers:
                        if local_buffer['mem_type'] == 'URAM':
                            BRAM_usage += URAM_array_predict_HLS(local_buffer['port_width'], \
                                local_buffer['buffer_depth'], local_buffer['partition_number'])
                y_pred_module[cnt] = URAM_usage
                cnt += 1

            y_test_module = y_test_module.to_numpy()
            logger.info('======== URAM ========')
            logger.info(f'Mean Absolute Error: {metrics.mean_absolute_error(y_test_module, y_pred_module)}')
            logger.info(f'Mean Squared Error: {metrics.mean_squared_error(y_test_module, y_pred_module)}')
            logger.info(f'Mean Absolute Percentage Error: {mean_absolute_percentage_error(y_test_module, y_pred_module)}')
            URAM_mape.append(mean_absolute_percentage_error(y_test_module, y_pred_module))
        
    logger.info('======== Module-Level Resource Model Validation Results ========')
    logger.info('FF Mean Absoulate Percentage Error (Arith. Mean): %.2f%%' %(mean(FF_mape)))
    logger.info('LUT Mean Absoulate Percentage Error (Arith. Mean): %.2f%%' %(mean(LUT_mape)))
    logger.info('DSP Mean Absoulate Percentage Error (Arith. Mean): %.2f%%' %(mean(DSP_mape)))
    logger.info('BRAM18K Mean Absoulate Percentage Error (Arith. Mean): %.2f%%' %(mean(BRAM18K_mape)))
    logger.info('URAM Mean Absoulate Percentage Error (Arith. Mean): %.2f%%' %(mean(URAM_mape)))

    # Validate on the whole design.
    df_test = df.loc[y_test.index.values.tolist(), :]
    FF_design_mape = []
    LUT_design_mape = []
    DSP_design_mape = []
    BRAM18K_design_mape = []
    URAM_design_mape = []

    for index, row in df_test.iterrows():
        #print(index)
        design_info = design_infos[index]
        df_design = df_test.loc[[index], :]
        res = predict_design_resource_usage(df_design, modules, fifos, design_info, work_dir)                 

        #print(design_info['BRAM18K'], res['BRAM18K'])
        #print(design_info['FF'], res['FF'])
        #print(design_info['LUT'], res['LUT'])

        FF_mape = mean_absolute_percentage_error(float(design_info['FF']), res['FF'])        
        LUT_mape = mean_absolute_percentage_error(float(design_info['LUT']), res['LUT'])
        DSP_mape = mean_absolute_percentage_error(float(design_info['DSP']), res['DSP'])        
        BRAM18K_mape = mean_absolute_percentage_error(float(design_info['BRAM18K']), res['BRAM18K'])
        URAM_mape = mean_absolute_percentage_error(float(design_info['URAM']), res['URAM'])

        FF_design_mape.append(FF_mape)
        LUT_design_mape.append(LUT_mape)
        DSP_design_mape.append(DSP_mape)
        BRAM18K_design_mape.append(BRAM18K_mape)
        URAM_design_mape.append(URAM_mape)

    logger.info('======== Design-Level Resource Model Validation Results ========')
    logger.info('FF Mean Absoulate Percentage Error (Arith. Mean): %.2f%%' %(mean(FF_design_mape)))
    logger.info('LUT Mean Absoulate Percentage Error (Arith. Mean): %.2f%%' %(mean(LUT_design_mape)))
    logger.info('DSP Mean Absoulate Percentage Error (Arith. Mean): %.2f%%' %(mean(DSP_design_mape)))
    logger.info('BRAM18K Mean Absoulate Percentage Error (Arith. Mean): %.2f%%' %(mean(BRAM18K_design_mape)))
    logger.info('URAM Mean Absoulate Percentage Error (Arith. Mean): %.2f%%' %(mean(URAM_design_mape)))    

def predict_design_resource_usage(df, modules, fifos, design_info, prj_dir, \
    target=['FF', 'LUT', 'DSP', 'BRAM18K', 'URAM']):
    """ Predict the resource usage for a single design on Xilinx platforms

    Parameters
    ----------
    df: dataframe
        A dataframe storing the information for the current design.
    modules: list
        A list containing all module names.
    fifos: list
        A list containing all FIFO names.
    design_info: dict
        A dictionary containing the design information.
    prj_dir: str
        Directory to the resource models.    
    target: list
        Resource types to predict.
    """
    resource = {'FF': 0, 'LUT': 0, 'DSP': 0, 'BRAM18K': 0, 'URAM': 0}    
    resource_all = {}

    # Predict FIFOs
    for fifo in fifos:
        if fifo in design_info['fifos']:
            # Query the library to get the data
            fifo_w = design_info['fifos'][fifo]['fifo_width'] * 8
            fifo_depth = design_info['fifos'][fifo]['fifo_depth']
            resource_info = FIFO_predict_xilinx(fifo_w, fifo_depth)
            FF = resource_info['FF']
            LUT = resource_info['LUT']
            BRAM = resource_info['BRAM18K']
            URAM = 0
            DSP = resource_info['DSP']
            resource_all[fifo] = {
                'FF': FF, 'LUT': LUT, 'BRAM18K': BRAM, 'URAM': URAM, 'DSP': DSP, \
                'n': design_info['fifos'][fifo]['fifo_cnt']}

    # Predict modules
    for module in modules:
        if module in design_info['modules']:
            df = df_feature_extract(df, module)
            module_feature_set = get_feature_set(module)

            FF = 0
            if 'FF' in target:
                # FF
                X = df.loc[:, module_feature_set]
                model_name = module + '_FF_model'
                joblib_file = prj_dir + '/' + model_name + '.pkl'
                if os.path.isfile(joblib_file):
                    model = joblib.load(joblib_file)
                    FF = np.asscalar(model.predict(X.to_numpy()))
                    # Add back the FF arrays if existing
                    if "local_buffers" in design_info['modules'][module]:
                        local_buffers = design_info['modules'][module]['local_buffers']
                        for local_buffer in local_buffers:
                            if local_buffer['mem_type'] == 'FF':
                                FF += FF_array_predict_HLS(local_buffer['port_width'], \
                                                           local_buffer['buffer_depth'])
            LUT = 0
            if 'LUT' in target:
                # LUT
                X = df.loc[:, module_feature_set]
                model_name = module + '_LUT_model'
                joblib_file = prj_dir + '/' + model_name + '.pkl'
                if os.path.isfile(joblib_file):
                    model = joblib.load(joblib_file)
                    LUT = np.asscalar(model.predict(X.to_numpy()))

            DSP = 0
            if 'DSP' in target:
                # DSP
                X = df.loc[:, module_feature_set]
                model_name = module + '_DSP_model'
                joblib_file = prj_dir + '/' + model_name + '.pkl'
                if os.path.isfile(joblib_file):
                    model = joblib.load(joblib_file)
                    DSP = np.asscalar(model.predict(X.to_numpy()))

            BRAM = 0
            if 'BRAM18K' in target:
                # BRAM                
                if 'local_buffers' in design_info['modules'][module]:
                    local_buffers = design_info['modules'][module]['local_buffers']
                    for local_buffer in local_buffers:
                        if local_buffer['mem_type'] == 'BRAM':
                            if 'array_map' in local_buffer:
                                # For horizontal mapping, we will merge two ping/pong buffers to one
                                BRAM += BRAM_array_predict_HLS(local_buffer['port_width'], \
                                    local_buffer['buffer_depth'] * 2, local_buffer['partition_number']) / 2
                            else:
                                BRAM += BRAM_array_predict_HLS(local_buffer['port_width'], \
                                    local_buffer['buffer_depth'], local_buffer['partition_number'])                            

            #if BRAM > 0:
            #    print(module, BRAM)

            URAM = 0
            if 'URAM' in target:
                # URAM                
                if 'local_buffers' in design_info['modules'][module]:
                    local_buffers = design_info['modules'][module]['local_buffers']
                    for local_buffer in local_buffers:
                        if local_buffer['mem_type'] == 'URAM':
                            URAM += URAM_array_predict_HLS(local_buffer['port_width'], \
                                local_buffer['buffer_depth'], local_buffer['partition_number'])

            resource_all[module] = {
                'FF': FF, 'LUT': LUT, 'BRAM18K': BRAM, 'URAM': URAM, 'DSP': DSP, \
                'n': design_info['modules'][module]['module_cnt']}        

    #pp = pprint.PrettyPrinter(indent=4)
    #pp.pprint(resource_all)

    # Aggregate the resource
    for inst in resource_all:
        # For FF/LUT/DSP prediction, if the module contains inner module, skip it.
        #is_outer_module = 0
        #if inst.find('boundary') != -1:
        #    if inst[:-9] + '_inter_trans' in resource_all:
        #        is_outer_module = 1
        #else:
        #    if inst + '_inter_trans' in resource_all:
        #        is_outer_module = 1
        is_inner_module = 0
        if inst.find('inter_trans') != -1 or inst.find('intra_trans') != -1:
            is_inner_module = 1
        #if not is_outer_module:
        #    resource['FF'] += resource_all[inst]['FF'] * resource_all[inst]['n']
        #    resource['LUT'] += resource_all[inst]['LUT'] * resource_all[inst]['n']
        #    resource['DSP'] += resource_all[inst]['DSP'] * resource_all[inst]['n']
        if is_inner_module:
            continue

        resource['FF'] += resource_all[inst]['FF'] * resource_all[inst]['n']
        resource['LUT'] += resource_all[inst]['LUT'] * resource_all[inst]['n']
        resource['DSP'] += resource_all[inst]['DSP'] * resource_all[inst]['n']
        resource['BRAM18K'] += resource_all[inst]['BRAM18K'] * resource_all[inst]['n']
        resource['URAM'] += resource_all[inst]['URAM'] * resource_all[inst]['n']

    ret = {}
    for r in resource:
        if r in target:
            ret[r] = int(resource[r])
        else:
            ret[r] = 0

    return ret

def mean_absolute_percentage_error(y_true, y_pred):    
    if isinstance(y_true, np.ndarray) and isinstance(y_pred, np.ndarray):
        error = np.divide((y_true - y_pred), y_true, out=(-y_pred), where=y_true!=0)
        return np.mean(np.abs(error)) * 100    
    else:    
        # scalar
        if y_true == 0:
            return abs(y_pred) * 100
        else:            
            return abs((y_true - y_pred) / y_true) * 100

def resource_valid(res, hw_info, range, target):
    """ Test if the resource usage is valid.

    Parameters
    ----------
    res: dict
        A dict containing the resource usage of the current design.
    hw_info: dict
        A dict containing the hardware platform information.
    thres: dict
        A dict containing the resource threshold.
    target: list
        A list containing the hw resource target to predict.

    Returns
    -------
    ret: boolean
    """
    for r in res:
        if r in target:
            usage = res[r]
            if usage > hw_info[r] * range[r][1]:
                return False
            if usage < hw_info[r] * range[r][0]:
                return False
    return True

def compute_res_util_score(res, hw_info):
    """ Compute a score for the current design utilization.

    We put different weights for different types of resource.
    URAM, DSP, BRAM18K: 0.3
    LUT: 0.2
    FF: 0.1
    """
    score = 0
    if 'FF' in res:
        score += 0.1 * float(int(res['FF'])) / hw_info['FF']
    if 'LUT' in res:
        score += 0.2 * float(int(res['LUT'])) / hw_info['LUT']
    if 'BRAM18K' in res:
        score += 0.3 * float(int(res['BRAM18K'])) / hw_info['BRAM18K']
    if 'DSP' in res:
        score += 0.3 * float(int(res['DSP'])) / hw_info['DSP']
    if 'URAM' in res:
        score += 0.3 * float(int(res['URAM'])) / hw_info['URAM']

    return score

def unit_test_predict_design_resource(design_dir, hw_info, model_path):
    design_info = extract_design_info(design_dir, 0)
    modules, fifos, df = convert_design_infos_to_df([design_info])
    kernel_id = design_info['kernel_id']        
    res_model_path = f'{model_path}/kernel{kernel_id}'
    res = predict_design_resource_usage(
        df, modules, fifos, design_info,
        res_model_path)
    # compute the ratio
    print(f"FF: {res['FF']}/{hw_info['FF']} ({res['FF']/hw_info['FF']:.2f})")
    print(f"LUT: {res['LUT']}/{hw_info['LUT']} ({res['LUT']/hw_info['LUT']:.2f})")
    print(f"BRAM18K: {res['BRAM18K']}/{hw_info['BRAM18K']} ({res['BRAM18K']/hw_info['BRAM18K']:.2f})")
    print(f"DSP: {res['DSP']}/{hw_info['DSP']} ({res['DSP']/hw_info['DSP']:.2f})")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="==== AutoSA Resource Model ====")
    parser.add_argument('-d', required=True, help='design directory')
    parser.add_argument('-i', required=True, help='hardware info')
    parser.add_argument('-m', required=True, help='resource model path')

    args = parser.parse_args()
    with open(args.i, 'r') as f:
        hw_info = json.load(f)
    unit_test_predict_design_resource(args.d, hw_info, args.m)