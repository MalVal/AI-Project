
import serial
import wave
import time
from datetime import datetime
from pathlib import Path

PORT = "COM3"
BAUD = 115200
RATE = 8000
SEGMENT_SECONDS = 60

OUTPUT_DIR = Path("enregistrements")
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

def new_wav():
    name = datetime.now().strftime("audio_%Y%m%d_%H%M%S.wav")
    path = OUTPUT_DIR / name

    w = wave.open(str(path), "wb")
    w.setnchannels(1)
    w.setsampwidth(1)
    w.setframerate(RATE)

    print(f"\nNouveau fichier : {path}")
    return w

ser = None
w = None

try:
    ser = serial.Serial(PORT, BAUD, timeout=2)
    time.sleep(2)
    ser.reset_input_buffer()

    print("Enregistrement en cours. Ctrl+C pour arrêter.")

    while True:
        w = new_wav()
        remaining = RATE * SEGMENT_SECONDS
        received = 0
        last_report = time.monotonic()

        try:
            while remaining > 0:
                chunk = ser.read(min(4096, remaining))

                if not chunk:
                    raise TimeoutError(
                        "Aucune donnée reçue pendant 2 secondes. "
                        "Vérifie l'Arduino et le port COM."
                    )

                w.writeframesraw(chunk)
                remaining -= len(chunk)
                received += len(chunk)

                if time.monotonic() - last_report >= 1:
                    seconds = received / RATE
                    print(
                        f"\rProgression : {seconds:.1f} / "
                        f"{SEGMENT_SECONDS} secondes",
                        end="",
                        flush=True
                    )
                    last_report = time.monotonic()

        finally:
            w.close()
            w = None

        print("\nFichier terminé : 60 secondes enregistrées.")

except KeyboardInterrupt:
    print("\nArrêt demandé.")

except (serial.SerialException, TimeoutError) as e:
    print(f"\nErreur : {e}")

finally:
    if w is not None:
        w.close()

    if ser is not None and ser.is_open:
        ser.close()

    print("Port série fermé.")
