import numpy as np
import smplx
import torch
import os
import json

def dump_to_binary(file_name, data, type=np.float32):
    np_vertices = np.array(data, dtype=type)
    np_vertices.tofile(file_name)

def dump_to_json(file_name, data):
    with open(file_name, 'w') as json_file:
        json.dump(data, json_file)

def dump_model(output_vertices, output_faces, root_folder):
    if not os.path.exists(root_folder):
        os.makedirs(root_folder)
    config = {
          "frame_number": len(output_vertices),
          "vertex_count": len(output_vertices[0]),
          "index_count": len(output_faces.flatten())
    }
    dump_to_json(root_folder + "config.json", config) # the info of model
    dump_to_binary(root_folder + "index.bin", output_faces.flatten().tolist(), np.uint32) # each face to 3 index
    dump_to_binary(root_folder + "vertices.bin", output_vertices, np.float32) # all vertices

class DumpSmplModel:
    def __init__(self, motion_pkl, model_path='./smpl_models', gender='MALE', root_folder="C:\\smpl_model\\"):
        self.motion_pkl = motion_pkl
        self.model_path = model_path
        self.gender = gender
        self.root_folder = root_folder

    def generate_model(self):
        smpl = smplx.SMPL(model_path=self.model_path, gender=self.gender, batch_size=1)
        output_vertices = smpl.forward(global_orient=torch.from_numpy(self.motion_pkl['smpl_poses'][:, :3]).float(),
                                       body_pose=torch.from_numpy(self.motion_pkl['smpl_poses'][:, 3:]).float()).vertices.detach().numpy()
        dump_model(output_vertices, smpl.faces, self.root_folder)
        print("Generated {} successfully".format(self.root_folder))







