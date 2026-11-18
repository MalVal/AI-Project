
from scipy.io import wavfile
from scipy import signal
from scipy.io.wavfile import WavFileWarning
import matplotlib.pyplot as plt
import numpy as np
import warnings
import glob
import os

warnings.filterwarnings("ignore", category=WavFileWarning)

DOSSIER_AUDIO = "Audio"
DOSSIER_SORTIE = "DataSetsAnalyse/Spectogramme_Welch_Val"

os.makedirs(f"{DOSSIER_SORTIE}/Spectrogramme", exist_ok=True)
os.makedirs(f"{DOSSIER_SORTIE}/PSD", exist_ok=True)

fichiers_audio = sorted(glob.glob(f"{DOSSIER_AUDIO}/audio_*.wav"))

for fichier in fichiers_audio:

    nom_audio = os.path.splitext(os.path.basename(fichier))[0]

    frequence_echantillonnage, audio = wavfile.read(fichier)

    if len(audio.shape) > 1:
        audio = audio[:, 0]

    audio = audio.astype(np.float64)

    frequences, temps, spectrogramme = signal.spectrogram(audio, fs=frequence_echantillonnage, nperseg=1024, noverlap=512)
    spectrogramme_db = 10 * np.log10(spectrogramme + 1e-10)
    plt.figure(figsize=(12, 6))
    plt.pcolormesh(temps, frequences, spectrogramme_db, shading="auto")

    # plt.xscale("linear")
    # plt.yscale("linear")
    # plt.xlim(0, 10)
    # plt.ylim(0, 10000)

    plt.axis("off")
    plt.subplots_adjust(left=0, right=1, top=1, bottom=0)
    plt.savefig(f"{DOSSIER_SORTIE}/Spectrogramme/{nom_audio}.png", dpi=300, bbox_inches="tight", pad_inches=0)
    plt.close()

    frequences_psd, puissance = signal.welch(audio, fs=frequence_echantillonnage, nperseg=1024)
    plt.figure(figsize=(12, 6))
    plt.plot(frequences_psd, 10 * np.log10(puissance + 1e-12))

    # plt.xscale("linear")
    # plt.yscale("linear")
    # plt.xlim(0, 10000)
    # plt.ylim(-100, 100)

    plt.axis("off")
    plt.subplots_adjust(left=0, right=1, top=1, bottom=0)
    plt.savefig(f"{DOSSIER_SORTIE}/PSD/{nom_audio}.png", dpi=300, bbox_inches="tight", pad_inches=0)
    plt.close()

    print(f"Images enregistrées : {nom_audio}")

print("Tous les spectrogrammes et PSD ont été enregistrés !")
