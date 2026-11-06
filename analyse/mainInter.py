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

os.makedirs("DataSetsAnalyse/Spectogramme/Spectogramme_Inter", exist_ok=True)

# Nombre de spectrogrammes par fichier audio
nombre_variations = 10

for engineType in enginesTypes:
    for i in range(70):

        frequence_echantillonnage, audio = wavfile.read(f"Audio/IDMT-ISA-ELECTRIC-ENGINE/test_cut/{engineType}/{motor}{i}.wav")

        if len(audio.shape) > 1:
            audio = audio[:, 0]

        # Convertir l'audio en float pour éviter les dépassements
        audio = audio.astype(np.float64)

        for j in range(nombre_variations):

            nouveau_audio = audio.copy()

            if j > 0:
                # Variation du volume
                volume = np.random.uniform(0.9, 1.1)
                nouveau_audio = nouveau_audio * volume

                # Ajout d'un bruit léger
                bruit = np.random.normal(0, np.std(audio) * 0.005, len(audio))
                nouveau_audio = nouveau_audio + bruit

                # Décalage temporel
                decalage = np.random.randint(-500, 501)
                nouveau_audio = np.roll(nouveau_audio, decalage)

            # Calcul du spectrogramme
            frequences, temps, spectrogramme = signal.spectrogram(nouveau_audio, fs=frequence_echantillonnage, nperseg=1024, noverlap=512)

            spectrogramme_db = 10 * np.log10(spectrogramme + 1e-10)

            plt.figure(figsize=(12, 6))
            plt.pcolormesh(temps, frequences, spectrogramme_db, shading="auto")

            plt.axis("off")
            plt.subplots_adjust(left=0, right=1, top=1, bottom=0)

            plt.savefig(f"DataSetsAnalyse/Spectogramme/Spectogramme_Inter/Spectrogramme_{engineType}_{i}_{j}.png", dpi=300, bbox_inches="tight", pad_inches=0)

            plt.close()

        print(f"Spectrogrammes enregistrés : {engineType} {i}")

print("Tous les spectrogrammes ont été enregistrés !")

