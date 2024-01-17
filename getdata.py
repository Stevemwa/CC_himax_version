import serial
import time
import sys
import wave
from playsound import playsound

# ser = serial.Serial()
# def get_com():
#     global ser
#     ser = serial.Serial()
#     i=1
#     while i<255:
#         name = 'COM'+str(i)
#         ser.open
#         try:
#             ser.is_open
#             ser=serial.Serial(name)
#             ser.baoudrate=115200
#             print(name)
#         except serial.serialutil.SerialException:
#             pass
#         i+=1
ser = serial.Serial("COM27", 921600, timeout=5)
ser.flushInput()  # Empty the buffer


def getdata():
    a = 0
    f = open('record.txt', 'a')
    buf = b''
    start = False
    while True:
        count = ser.inWaiting()  # Get Serial Buffer Data
        if count != 0:
            recv = ser.read(count)
            buf += recv
            if buf.find(b'PDM example, 16KHz mono, record a single shot') != -1:
                print("Recording begins")
                buf = b''
            if buf.find(b'PDM example, print done') != -1:
                start = False
                buf = b''
                break
            elif buf.find(b'PDM example, record done') != -1:
                start = True
                print("End of recording, start conversion")
                buf = b''
            else:
                if start:
                    f.write(recv.decode('utf-8'))



def pcm2wav(pcm_file, wav_file, channels=1, bits=32, sample_rate=16000):
    cnt = 0
    pcmdata_last = '0000'
    pcmf = open(pcm_file, 'r')
    wavfile = wave.open(wav_file, 'wb')
    pcmdata_len = len(pcmf.read())
    pcmf.seek(0)

    wavfile.setnchannels(channels)
    wavfile.setsampwidth(bits // 8)
    wavfile.setframerate(sample_rate)

    while cnt < pcmdata_len:
        # we only use the data which is in right length
        pcmdata = pcmf.readline()
        len_tmp = len(pcmdata)
        if len(pcmdata) == 5:
            pcmdata = pcmdata[0:4]
            pcmdata_last = pcmdata
            temp = int(pcmdata, 16)
        else:
            temp = int(pcmdata_last, 16)

        wavfile.writeframes(temp.to_bytes(2, 'little'))
        cnt = cnt + len_tmp

    wavfile.close()
    pcmf.close()


if __name__ == '__main__':
    file = open("record.txt", 'w').close()
    # get_com()
    getdata()
    pcm2wav('record.txt', 'record.wav')
    # b=input('Type enter to start playback')
    # if b=='':
    #     playsound('record.wav')
    #     #    pcm2wav(sys.argv[1], sys.argv[2])
    print("If you can hear the recording, test PASS! Otherwise test Fail!")
