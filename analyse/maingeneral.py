from scipy.io import wavfile
from scipy import signal
from scipy.io.wavfile import WavFileWarning
import matplotlib.pyplot as plt
import numpy as np
import warnings
import os

warnings.filterwarnings("ignore", category=WavFileWarning)

motor = "atmo_high"
enginesTypes = ["engine1_good", "engine2_broken", "engine3_heavyload"]
os.makedirs("DataSetsAnalyse/Spectogramme/Spectogramme_gene", exist_ok=True)

for engineType in enginesTypes:

    frequence_echantillonnage, audio = wavfile.read(f"Audio/IDMT-ISA-ELECTRIC-ENGINE/test/{engineType}/{motor}.wav")

    if len(audio.shape) > 1:
        audio = audio[:, 0]

    frequences, temps, spectrogramme = signal.spectrogram(audio, fs=frequence_echantillonnage, nperseg=1024, noverlap=512)

    spectrogramme_db = 10 * np.log10(spectrogramme + 1e-10)

    plt.figure(figsize=(12, 6))
    plt.pcolormesh(temps, frequences, spectrogramme_db, shading="auto")

    plt.axis("off")
    plt.subplots_adjust(left=0, right=1, top=1, bottom=0)

    plt.savefig(f"DataSetsAnalyse/Spectogramme/Spectogramme_gene/Spectrogramme_{engineType}.png", dpi=300, bbox_inches="tight", pad_inches=0)
    plt.close()
    print(f"Spectrogramme enregistré : {engineType}")
print("Tous les spectrogrammes ont été enregistrés !")

