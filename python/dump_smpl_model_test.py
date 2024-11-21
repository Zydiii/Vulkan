import pickle
from dump_smpl_model import DumpSmplModel

if __name__ == '__main__':
    motion_filename = "gJB_sBM_cAll_d09_mJB5_ch01_mBR0"
    with open(f'./{motion_filename}.pkl', 'rb') as motion_file:
        motion_pkl = pickle.load(motion_file)
    dump_smpl_model = DumpSmplModel(motion_pkl)
    dump_smpl_model.generate_model()