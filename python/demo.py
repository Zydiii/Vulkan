import numpy as np
import pickle
import librosa
from scipy.spatial import transform
import torch # 1.8.0
import smplx
import trimesh
import vedo
import json
import os


def encode_input_audio(input_audio_wav, audio_name, sr):
    def _get_tempo(audio_name):
        if audio_name[:3] in ['mBR', 'mPO', 'mLO', 'mMH', 'mLH', 'mWA', 'mKR', 'mJS', 'mJB']:
            return int(audio_name[3]) * 10 + 80
        elif audio_name[:3] == 'mHO':
            return int(audio_name[3]) * 5 + 110

    envelope = librosa.onset.onset_strength(y=input_audio_wav, sr=sr)
    mfcc = librosa.feature.mfcc(y=input_audio_wav, sr=sr).T
    chroma = librosa.feature.chroma_cens(y=input_audio_wav, sr=sr).T

    peaks = np.zeros_like(envelope, dtype=np.float32)
    peak_indexs = librosa.onset.onset_detect(onset_envelope=envelope, sr=sr)
    peaks[peak_indexs] = 1.0

    beats = np.zeros_like(envelope, dtype=np.float32)
    _, beat_indexs = librosa.beat.beat_track(onset_envelope=envelope, sr=sr, start_bpm=_get_tempo(audio_name))
    beats[beat_indexs] = 1.0

    input_audio_npy = np.concatenate([envelope[:, None], mfcc, chroma, peaks[:, None], beats[:, None]], axis=-1)
    return input_audio_npy

def encode_input_motion(input_motion_pkl):
    input_trans = input_motion_pkl['smpl_trans'] / input_motion_pkl['smpl_scaling']
    input_poses = input_motion_pkl['smpl_poses'].reshape(-1, 3)
    input_poses = transform.Rotation.from_rotvec(input_poses).as_matrix().reshape(-1, 216)

    input_motion_npy = np.concatenate([input_trans, input_poses], axis=-1)
    input_motion_npy = np.pad(input_motion_npy, ((0, 0), (6, 0)), mode='constant')
    return input_motion_npy

def decode_output_motion(output_motion_npy):
    def _get_eyemat(n, batch_shape):
        iden = np.zeros(np.concatenate([batch_shape, [n, n]]))
        iden[..., 0, 0] = 1.0
        iden[..., 1, 1] = 1.0
        iden[..., 2, 2] = 1.0
        return iden

    def _get_closest_rotmats(rotmats):
        u, _, vh = np.linalg.svd(rotmats)
        r_closest = np.matmul(u, vh)
        det = np.linalg.det(r_closest)
        iden = _get_eyemat(3, det.shape)
        iden[..., 2, 2] = np.sign(det)
        r_closest = np.matmul(np.matmul(u, iden), vh)
        return r_closest

    output_trans = output_motion_npy[:, 6:9]
    output_poses = output_motion_npy[:, 9:].reshape(-1, 3, 3)
    output_poses = _get_closest_rotmats(output_poses)
    output_poses = transform.Rotation.from_matrix(output_poses).as_rotvec().reshape(-1, 72)

    output_motion_pkl = {'smpl_poses': output_poses, 'smpl_trans': output_trans}
    return output_motion_pkl

def demo(audio_name, motion_name, seq_index):
    # intput_audio.wav -> input_audio.npy
    SR = 60 * 512 # fps*hop_length
    input_audio_wav, _ = librosa.load(f'./{audio_name}.wav', sr=SR)
    input_audio_npy = encode_input_audio(input_audio_wav, audio_name, SR)
    np.save(f'./{audio_name}.npy', input_audio_npy)

    # input_motion.pkl -> input_motion.npy
    with open(f'./{motion_name}.pkl', 'rb') as input_motion_file:
        input_motion_pkl = pickle.load(input_motion_file)
    input_motion_npy = encode_input_motion(input_motion_pkl)
    np.save(f'./{motion_name}.npy', input_motion_npy)

    # output_motion.npy -> output_motion.pkl
    output_motion_npy = np.load(f'./{motion_name}_{audio_name}.npy')
    output_motion_pkl = decode_output_motion(output_motion_npy)
    with open(f'./{motion_name}_{audio_name}.pkl', 'wb') as output_motion_file:
        pickle.dump(output_motion_pkl, output_motion_file)

    # render input/output motion
    smpl = smplx.SMPL(model_path='./smpl_models', gender='MALE', batch_size=1)

    # input_vertices = smpl.forward(global_orient=torch.from_numpy(input_motion_pkl['smpl_poses'][:, :3]).float(),
    #                               body_pose=torch.from_numpy(input_motion_pkl['smpl_poses'][:, 3:]).float(),
    #                               transl=torch.from_numpy(input_motion_pkl['smpl_trans']).float()
    # ).vertices.detach().numpy()
    # input_mesh = trimesh.Trimesh(input_vertices[seq_index], smpl.faces)
    # input_mesh.visual.face_colors = [200, 200, 200, 100]
    # vedo.show(input_mesh, interactive=True)
    # exit()

    output_vertices = smpl.forward(global_orient=torch.from_numpy(output_motion_pkl['smpl_poses'][:, :3]).float(),
                                   body_pose=torch.from_numpy(output_motion_pkl['smpl_poses'][:, 3:]).float(),
                                   transl=torch.from_numpy(output_motion_pkl['smpl_trans']).float()
    ).vertices.detach().numpy()
    # 将 numpy 数组转换为列表
    output_vertices_list = output_vertices.tolist()
    smpl_faces_list = smpl.faces.tolist()

    # 创建一个字典来保存数据
    data = {
        "output_vertices": output_vertices_list,
        "smpl_faces": smpl_faces_list
    }

    # 将数据保存到 .json 文件
    # with open('C:\\smpl_data.json', 'w') as json_file:
    #     json.dump(data, json_file)

    print("数据已成功保存到 smpl_data.json")

    dump_model(output_vertices, smpl.faces)

    for seq_index in range(100):
        output_mesh = trimesh.Trimesh(vertices=output_vertices[0], faces=smpl.faces)
        output_normals = output_mesh.vertex_normals
        output_mesh.visual.face_colors = [200, 200, 200, 200]
        vedo.show(output_mesh, interactive=False)
    # output_mesh = trimesh.Trimesh(output_vertices[seq_index], smpl.faces)
    # output_mesh.visual.face_colors = [200, 200, 200, 200]
    # vedo.show(output_mesh, interactive=True)
    exit()

def dump_model(output_vertices, output_faces, root_folder="C:\\dump_model\\"):
    if not os.path.exists(root_folder):
        os.makedirs(root_folder)
    config = {
          "frame_number": len(output_vertices),
          "vertex_count": len(output_vertices[0]),
          "index_count": len(output_faces.flatten())
    }
    dump_to_json(root_folder + "config.json", config)
    faces = {"faces": output_faces.flatten().tolist()}
    # dump_to_json(root_folder + "faces.json", faces)
    dump_to_binary(root_folder + "index.bin", output_faces.flatten().tolist(), np.uint32)
    dump_to_binary(root_folder + "vertices.bin", output_vertices, np.float32)
    # i = 0
    # for per_output_vertices in output_vertices:
    #
    #     dump_to_binary(root_folder + "vertices.bin", per_output_vertices)
    #     output_mesh = trimesh.Trimesh(vertices=per_output_vertices, faces=output_faces)
    #     glb_data = trimesh.exchange.gltf.export_glb(output_mesh)
    #     mesh2 = output_mesh.copy()
    #     mesh2.apply_scale(1.1)
    #     # glb_data.export('stuff.gltf')
    #     with open('output.glb', 'wb') as f:
    #         f.write(glb_data)
    #
    #     output_normals = output_mesh.vertex_normals
    #     data = {
    #         "vertices": per_output_vertices.tolist(),
    #         "normals": output_normals.tolist()
    #     }
    #     dump_to_json(root_folder + "models_frame_{}.json".format(i), data)
    #     i = i + 1

def dump_to_binary(file_name, data, type=np.float32):
    np_vertices = np.array(data, dtype=type)
    np_vertices.tofile(file_name)

def dump_to_json(file_name, data):
    with open(file_name, 'w') as json_file:
        json.dump(data, json_file)

if __name__ == '__main__':
    AUDIO_NAME = 'mBR0'
    MOTION_NAME = 'gJB_sBM_cAll_d09_mJB5_ch01'
    SEQ_INDEX = 400 # index < 1320

    demo(AUDIO_NAME, MOTION_NAME, SEQ_INDEX)
