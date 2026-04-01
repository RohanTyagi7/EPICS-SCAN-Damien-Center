import serial
import time
import os

serial_port = 'COM5'
baud_rate = 115200
output_folder_path = 'output'


def read_serial_func():
    output_file_path = input('Enter output file name: ')
    full_file_path = os.path.join(output_folder_path, output_file_path)

    sample_time = int(input('Enter sample time (s): '))
    
    with open(full_file_path, 'w+') as file:
        ser = serial.Serial(serial_port, baud_rate)

        print("Recording data in 5...")
        time.sleep(1)
        for i in range(4, 1, -1):
            print(f'{i}...')
            time.sleep(1)

        start_time = time.time()

        while time.time() < start_time + sample_time:
            line = ser.readline()
            line = line.decode('utf-8')
            print(line)
            file.write(line)

    print(f"Results written to '{full_file_path}'")


def main():
    read_serial_func()

    while input('Again? (y/n): ').lower() == 'y':
        read_serial_func()


if __name__ == '__main__':
    main()