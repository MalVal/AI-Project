import os
import glob
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

from scipy import signal

DOSSIER_CSV = "Accelerometre"
DOSSIER_SORTIE = "DataSetsAnalyse/Accelerometre_Val"

GRAPHIQUES = ["XYZ", "FFT", "PSD", "Spectrogramme", "3D"]

for graphique in GRAPHIQUES:
    os.makedirs(f"{DOSSIER_SORTIE}/{graphique}", exist_ok=True)

fichiers_csv = sorted(glob.glob(f"{DOSSIER_CSV}/accel_data_*.csv"))

for fichier in fichiers_csv:

    nom = os.path.splitext(os.path.basename(fichier))[0]

    df = pd.read_csv(fichier, header=None, names=["Temps", "X", "Y", "Z"])

    df = df.sort_values("Temps").drop_duplicates("Temps")

    if len(df) < 4:
        print(f"Fichier ignoré (trop court) : {nom}")
        continue

    temps = (df["Temps"].to_numpy() - df["Temps"].iloc[0]) / 1000

    x = df["X"].to_numpy()
    y = df["Y"].to_numpy()
    z = df["Z"].to_numpy()

    dt = np.median(np.diff(temps))
    fs = 1 / dt

    temps_regulier = np.arange(0, temps[-1], dt)

    xr = np.interp(temps_regulier, temps, x)
    yr = np.interp(temps_regulier, temps, y)
    zr = np.interp(temps_regulier, temps, z)

    axes = {"X": xr, "Y": yr, "Z": zr}

    axes = {axe: valeurs - np.mean(valeurs)
            for axe, valeurs in axes.items()}

    plt.figure(figsize=(12, 6))

    plt.plot(temps, x, label="X")
    plt.plot(temps, y, label="Y")
    plt.plot(temps, z, label="Z")

    plt.xlabel("Temps (s)")
    plt.ylabel("Accélération (g)")
    plt.title("Accélération XYZ")
    plt.legend()
    plt.grid(True)

    # plt.xscale("linear")
    # plt.yscale("linear")
    # plt.xlim(0, 35)
    # plt.ylim(-2, 2)

    plt.tight_layout()
    plt.savefig(f"{DOSSIER_SORTIE}/XYZ/{nom}.png", dpi=300)
    plt.close()

    fig, graphiques = plt.subplots(3, 1, figsize=(12, 9))

    for graphique, (axe, valeurs) in zip(graphiques, axes.items()):

        frequences, t, spectrogramme = signal.spectrogram(valeurs, fs=fs, nperseg=min(64, len(valeurs)), noverlap=min(32, len(valeurs) // 2))

        spectrogramme_db = 10 * np.log10(spectrogramme + 1e-12)

        image = graphique.pcolormesh(t, frequences, spectrogramme_db, shading="auto")

        graphique.set_title(f"Spectrogramme {axe}")
        graphique.set_xlabel("Temps (s)")
        graphique.set_ylabel("Fréquence (Hz)")

        fig.colorbar(image, ax=graphique, label="PSD (dB/Hz)")

        # graphique.set_xscale("linear")
        # graphique.set_yscale("linear")
        # graphique.set_xlim(0, 35)
        # graphique.set_ylim(0, 2.5)

    plt.tight_layout()
    plt.savefig(f"{DOSSIER_SORTIE}/Spectrogramme/{nom}.png", dpi=300)
    plt.close()

    fig = plt.figure(figsize=(12, 8))
    ax = fig.add_subplot(111, projection="3d")

    points = ax.scatter(x, y, z, c=temps, cmap="viridis", s=10)

    ax.set_xlabel("X (g)")
    ax.set_ylabel("Y (g)")
    ax.set_zlabel("Z (g)")
    ax.set_title("Accélération XYZ - 3D")

    fig.colorbar(points, ax=ax, label="Temps (s)")

    # ax.set_xscale("linear")
    # ax.set_yscale("linear")
    # ax.set_zscale("linear")
    # ax.set_xlim(-2, 2)
    # ax.set_ylim(-2, 2)
    # ax.set_zlim(-2, 2)

    plt.tight_layout()
    plt.savefig(f"{DOSSIER_SORTIE}/3D/{nom}.png", dpi=300)
    plt.close()

    print(f"5 graphiques enregistrés : {nom} (fs = {fs:.2f} Hz)")

print("Tous les graphiques ont été enregistrés !")
