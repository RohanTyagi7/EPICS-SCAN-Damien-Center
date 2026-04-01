import os
import numpy as np
import re
import matplotlib.pyplot as plt


def analyze_desync_data(folder_path: str) -> None:
    for filename in os.listdir(folder_path):
        pattern = r"Time \(us\): ([\d]+)\| Time since motion \(us\): ([\d]+) \| Time since ToF \(us\): ([\d]+)\| Desync \(us\): ([\d]+)"
        with open(os.path.join(folder_path, filename)) as file:
            print(os.path.join(folder_path, filename))

            t_arr = []
            motion_arr = []
            tof_arr = []
            desync_arr = []

            t_start = None

            while line := file.readline():
                result = re.findall(pattern, line)

                result = result[0]

                t_val, motion_val, tof_val, desync_val = result[0], result[1], result[2], result[3]

                if t_start is None:
                    t_start = int(t_val)

                t_arr.append((int(t_val)-t_start) / 1e6)
                motion_arr.append(int(motion_val) / 1e6)
                tof_arr.append(int(tof_val) / 1e6)
                desync_arr.append(int(desync_val) / 1e6)
            
            t_arr = np.array(t_arr)
            motion_arr = np.array(motion_arr)
            tof_arr = np.array(tof_arr)
            desync_arr = np.array(desync_arr)
            
            plt.plot(t_arr, motion_arr, "r", label="time_since_motion")
            plt.plot(t_arr, tof_arr, "blue", label="time_since_tof")
            plt.plot(t_arr, desync_arr, "g", label="desync")
            plt.xlabel("Time (s)")
            plt.ylabel("Time (s)")
            plt.legend(loc="upper left")
            plt.show()


def analyze_motion_data(folder_path: str) -> None:
    for filename in os.listdir(folder_path):
        if filename[-3:] == 'png':
            continue

        pattern = r"Time \(us\): ([\d]+) \| Motion: (.*)"
        with open(os.path.join(folder_path, filename)) as file:
            print(os.path.join(folder_path, filename))

            t_arr = []
            motion_arr = []

            t_start = None

            while line := file.readline():
                result = re.findall(pattern, line)

                result = result[0]

                t_val, motion_val = result[0], result[1]

                if t_start is None:
                    t_start = int(t_val)
                
                if motion_val == 'No':
                    motion_val = 0
                else:
                    motion_val = 1

                t_arr.append((int(t_val)-t_start) / 1e6)
                motion_arr.append(motion_val)
            
            t_arr = np.array(t_arr)
            motion_arr = np.array(motion_arr)

            print(filename)
            
            plt.plot(t_arr, motion_arr, "r")
            plt.xlabel("Time (s)")
            plt.ylabel("Motion Detected? (0/1)")
            plt.text(7, 1.1, f'Avg. Motion: {sum(motion_arr) / len(motion_arr)}')
            plt.show()


def analyze_human_presence_data(folder_path: str) -> None:
    for filename in os.listdir(folder_path):
        if filename[-3:] == 'png':
            continue

        pattern_1 = r"([\d]+) \| (Yes|No)(.*)"
        pattern_2 = r" \| MOVING = ([\d]+) \| ([\d]+)cm"
        with open(os.path.join(folder_path, filename)) as file:
            print(os.path.join(folder_path, filename))

            t_arr = []
            motion_arr = []

            t_start = None

            while line := file.readline():
                if line.isspace(): continue

                result_1 = re.findall(pattern_1, line)[0]

                t_val, motion_val, extra_stuff = result_1[0], result_1[1], result_1[2]

                if t_start is None:
                    t_start = int(t_val)
                
                if motion_val == 'No':
                    motion_val = 0
                else:
                    result_2 = re.findall(pattern_2, extra_stuff)[0]

                    motion_val = int(result_2[0])
                    # range also collected

                t_arr.append((int(t_val)-t_start) / 1e6)
                motion_arr.append(motion_val)
            
            t_arr = np.array(t_arr)
            motion_arr = np.array(motion_arr)

            print(filename)
            
            plt.plot(t_arr, motion_arr, "r")
            plt.xlabel("Time (s)")
            plt.ylabel("Detected Moton Value (0-100)")
            plt.text(30, 110, f'Avg. Motion: {sum(motion_arr) / len(motion_arr):.3f}')
            plt.show()


def analyze_tof_data(folder_path: str) -> None:
    for filename in os.listdir(folder_path):
        pattern = r"Time \(us\): ([\d]+) \| Tripped: (.*)"
        with open(os.path.join(folder_path, filename)) as file:
            print(os.path.join(folder_path, filename))

            t_arr = []
            tof_arr = []

            t_start = None

            while line := file.readline():
                result = re.findall(pattern, line)

                result = result[0]

                t_val, tof_val = result[0], result[1]

                if t_start is None:
                    t_start = int(t_val)
                
                if tof_val == 'No':
                    tof_val = 0
                else:
                    tof_val = 1

                t_arr.append((int(t_val)-t_start) / 1e6)
                tof_arr.append(tof_val)
            
            t_arr = np.array(t_arr)
            tof_arr = np.array(tof_arr)

            print(filename)
            
            plt.plot(t_arr, tof_arr, "r")
            plt.xlabel("Time (s)")
            plt.ylabel("ToF Tripped? (0/1)")
            plt.show()


def analyze_test_case_data(folder_path: str) -> None:
    for filename in os.listdir(folder_path):

        pattern = r"Time \(ms\): ([\d]+) \| Time since motion \(ms\): ([\d]+) \| Time since ToF \(ms\): ([\d]+)"
        with open(os.path.join(folder_path, filename)) as file:
            print(os.path.join(folder_path, filename))

            t_arr = []
            motion_arr = []
            tof_arr = []

            t_start = None

            while line := file.readline():
                result = re.findall(pattern, line)

                result = result[0]

                t_val, motion_val, tof_val = result[0], result[1], result[2]

                if t_start is None:
                    t_start = int(t_val)

                t_arr.append((int(t_val)-t_start) / 1e3)
                motion_arr.append(int(motion_val) / 1e3)
                tof_arr.append(int(tof_val) / 1e3)
            
            t_arr = np.array(t_arr)
            motion_arr = np.array(motion_arr)
            tof_arr = np.array(tof_arr)

            print(filename)
            
            plt.plot(t_arr, motion_arr, "r", label="time_since_motion")
            plt.plot(t_arr, tof_arr, "blue", label="time_since_tof")
            plt.xlabel("Time (s)")
            plt.ylabel("Time (s)")
            plt.legend(loc="upper left")
            plt.show()


def analyze_pir_hp_algo_data(folder_path: str) -> None:
    for filename in os.listdir(folder_path):
        pattern = r"([\d]+) \| (WATCHING|WAITING) \| ([\d]+) \| ([\d]+) \| (null|[\d]+) \| ([\d]+) \| (.*)"
        with open(os.path.join(folder_path, filename)) as file:
            print(os.path.join(folder_path, filename))

            t_arr = []
            state_arr = []
            motion_time_arr = []
            room_empty_arr = []
            distance_arr = []
            est_distance_arr = []
            motion_val_arr = []

            t_start = None

            while line := file.readline():
                if line.isspace(): continue

                result = re.findall(pattern, line)[0]

                t, state, m_time, room_empty, distance, est_distance, m_val = result

                if t_start is None:
                    t_start = int(t)

                t_arr.append((int(t) - t_start) / 1e3)
                state_arr.append(1 if state == 'WAITING' else 2)
                motion_time_arr.append(int(m_time) / 1e3)
                room_empty_arr.append(int(room_empty) / 1e3)
                distance_arr.append(0 if distance == 'null' else int(distance) / 1e2 * 3.28084)
                est_distance_arr.append(int(est_distance))
                motion_val_arr.append(float(m_val))
            
            t_arr = np.array(t_arr)
            state_arr = np.array(state_arr)
            motion_time_arr = np.array(motion_time_arr)
            room_empty_arr = np.array(room_empty_arr)
            distance_arr = np.array(distance_arr)
            est_distance_arr = np.array(est_distance_arr)
            motion_val_arr = np.array(motion_val_arr)

            print(filename)
            
            plt.plot(t_arr, motion_val_arr, "r", label="Instantaneous Motion Value (0-100)")
            # plt.plot(t_arr, avg_motion_val_arr, label="Average Motion Value (0-100)")
            plt.title("Motion Values vs. Time")
            plt.xlabel("Time (s)")
            plt.ylabel("Motion Value")
            # plt.legend(loc="upper left")
            plt.show()

            plt.plot(t_arr, distance_arr, "r")
            plt.plot(t_arr, est_distance_arr, "b")
            plt.title("Detected Distance vs. Time")
            plt.xlabel("Time (s)")
            plt.ylabel("Detected Distance (ft)")
            plt.legend(loc="upper left")
            plt.show()

            
def main() -> None:
    analyze_pir_hp_algo_data('output')


if __name__ == '__main__':
    main()