import serial, wave, time
from datetime import datetime

PORT = "/dev/ttyACM0"   # vérifie avec: ls /dev/ttyACM* /dev/ttyUSB*
BAUD = 115200
RATE = 8000
SEGMENT_SECONDS = 600   # un fichier toutes les 10 min

ser = serial.Serial(PORT, BAUD)
time.sleep(2)           # l'Arduino redémarre à l'ouverture du port
ser.reset_input_buffer()

def new_wav():
    name = datetime.now().strftime("audio_%Y%m%d_%H%M%S.wav")
    w = wave.open(name, "wb")
    w.setnchannels(1)
    w.setsampwidth(1)   # 8 bits non signé
    w.setframerate(RATE)
    return w

while True:
    w = new_wav()
    bytes_to_read = RATE * SEGMENT_SECONDS
    remaining = bytes_to_read
    while remaining > 0:
        chunk = ser.read(min(4096, remaining))
        w.writeframes(chunk)
        remaining -= len(chunk)
    w.close()