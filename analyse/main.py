
from scipy.io import wavfile
from scipy import signal
from scipy.io.wavfile import WavFileWarning
import matplotlib.pyplot as plt
import numpy as np
import librosa
import pywt
import warnings
import os

warnings.filterwarnings("ignore", category=WavFileWarning)

motor = "atmo_high"
enginesTypes = ["engine1_good", "engine2_broken", "engine3_heavyload"]

dossier = "DataSetsAnalyse/Spectogramme"
duree_extrait = 5  # Secondes au milieu de l'audio

representations = [
    "Waveform",
    "Spectrogramme",
    "MelSpectrogramme",
    "PSD",
    "Scalogramme",
    "MFCC"
]

for representation in representations:
    os.makedirs(f"{dossier}/{representation}", exist_ok=True)


def sauvegarder(nom, engineType):
    plt.axis("off")
    plt.subplots_adjust(left=0, right=1, top=1, bottom=0)

    plt.savefig(
        f"{dossier}/{nom}/{nom}_{engineType}.png",
        dpi=300,
        bbox_inches="tight",
        pad_inches=0
    )
    plt.close()


for engineType in enginesTypes:

    # Chargement du fichier
    frequence_echantillonnage, audio = wavfile.read(
        f"Audio/IDMT-ISA-ELECTRIC-ENGINE/test/{engineType}/{motor}.wav"
    )

    if len(audio.shape) > 1:
        audio = audio[:, 0]

    # Conversion en float et normalisation PCM
    if np.issubdtype(audio.dtype, np.integer):
        info = np.iinfo(audio.dtype)
        audio = audio.astype(np.float64)

        if info.min == 0:
            audio = (audio - (info.max + 1) / 2) / ((info.max + 1) / 2)
        else:
            audio = audio / max(abs(info.min), info.max)
    else:
        audio = audio.astype(np.float64)

    # Découpe au milieu de l'audio
    nombre_echantillons = min(
        int(duree_extrait * frequence_echantillonnage),
        len(audio)
    )

    milieu = len(audio) // 2
    debut = milieu - nombre_echantillons // 2
    audio = audio[debut:debut + nombre_echantillons]

    # 1. Waveform
    temps_audio = np.arange(len(audio)) / frequence_echantillonnage

    plt.figure(figsize=(12, 6))
    plt.plot(temps_audio, audio, linewidth=0.5)
    sauvegarder("Waveform", engineType)

    # 2. Spectrogramme classique
    frequences, temps, spectrogramme = signal.spectrogram(
        audio,
        fs=frequence_echantillonnage,
        nperseg=1024,
        noverlap=512
    )

    spectrogramme_db = 10 * np.log10(spectrogramme + 1e-10)

    plt.figure(figsize=(12, 6))
    plt.pcolormesh(
        temps, frequences, spectrogramme_db,
        shading="auto"
    )
    sauvegarder("Spectrogramme", engineType)

    # 3. Mel-spectrogramme
    mel = librosa.feature.melspectrogram(
        y=audio,
        sr=frequence_echantillonnage,
        n_fft=1024,
        hop_length=512,
        n_mels=128
    )

    mel_db = librosa.power_to_db(mel, ref=1.0)

    plt.figure(figsize=(12, 6))
    plt.imshow(mel_db, aspect="auto", origin="lower")
    sauvegarder("MelSpectrogramme", engineType)

    # 4. PSD
    frequences_psd, puissance = signal.welch(
        audio,
        fs=frequence_echantillonnage,
        nperseg=1024
    )

    plt.figure(figsize=(12, 6))
    plt.plot(
        frequences_psd,
        10 * np.log10(puissance + 1e-12)
    )
    sauvegarder("PSD", engineType)

    # 5. Scalogramme (Wavelet)
    # Analyse sur une version réduite pour limiter le calcul
    pas = max(1, int(frequence_echantillonnage / 4000))
    audio_reduit = audio[::pas]
    frequence_reduite = frequence_echantillonnage / pas

    scales = np.arange(1, 129)

    coefficients, frequences_cwt = pywt.cwt(
        audio_reduit,
        scales,
        "morl",
        sampling_period=1 / frequence_reduite
    )

    temps_cwt = np.arange(len(audio_reduit)) / frequence_reduite
    ordre = np.argsort(frequences_cwt)

    plt.figure(figsize=(12, 6))
    plt.pcolormesh(
        temps_cwt,
        frequences_cwt[ordre],
        np.abs(coefficients[ordre]),
        shading="auto"
    )
    sauvegarder("Scalogramme", engineType)

    # 6. MFCC
    mfcc = librosa.feature.mfcc(
        y=audio,
        sr=frequence_echantillonnage,
        n_mfcc=20,
        n_fft=1024,
        hop_length=512
    )

    plt.figure(figsize=(12, 6))
    plt.imshow(mfcc, aspect="auto", origin="lower")
    sauvegarder("MFCC", engineType)

    print(f"Représentations enregistrées : {engineType}")

print("Toutes les représentations ont été enregistrées !")
