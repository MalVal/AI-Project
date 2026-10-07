from scipy.io import wavfile
from scipy import signal
from scipy.io.wavfile import WavFileWarning
import matplotlib.pyplot as plt
import numpy as np
import warnings
import os

warnings.filterwarnings("ignore", category=WavFileWarning)

motor = "atmo_high_"
enginesTypes = ["engine1_good", "engine2_broken", "engine3_heavyload"]
os.makedirs("DataSetsAnalyse/Spectogramme", exist_ok=True)

for engineType in enginesTypes:
    i = 0
    while i < 70:

        frequence_echantillonnage, audio = wavfile.read(f"Audio/IDMT-ISA-ELECTRIC-ENGINE/test_cut/{engineType}/{motor}{i}.wav")

        if len(audio.shape) > 1:
            audio = audio[:, 0]

        frequences, temps, spectrogramme = signal.spectrogram(audio, fs=frequence_echantillonnage, nperseg=1024, noverlap=512)

        spectrogramme_db = 10 * np.log10(spectrogramme + 1e-10)

        plt.figure(figsize=(12, 6))
        plt.pcolormesh(temps, frequences, spectrogramme_db, shading="auto")

        plt.axis("off")
        plt.subplots_adjust(left=0, right=1, top=1, bottom=0)

        #plt.xlabel("Temps (s)")
        #plt.ylabel("Fréquence (Hz)")
        #plt.title(f"Spectrogramme {engineType} {i}")
        #plt.colorbar(label="Puissance (dB)")
        plt.savefig(f"DataSetsAnalyse/Spectogramme/Spectrogramme_{engineType}_{i}.png", dpi=300, bbox_inches="tight", pad_inches=0)
        plt.close()
        print(f"Spectrogramme enregistré : {engineType} {i}")
        i = i + 1
print("Tous les spectrogrammes ont été enregistrés !")

