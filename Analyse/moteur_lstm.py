"""Comparaison de modeles pour spectrogrammes moteurs (LSTM, LSTM ameliore, CNN).

Installer: pip install tensorflow opencv-python numpy scikit-learn matplotlib
Lancer: python moteur_lstm_v2.py
Ou: python moteur_lstm_v2.py --dataset "C:/chemin/dataset" --epochs 100
Predire: python moteur_lstm_v2.py --predict "C:/chemin/image.png" --model lstm

Les jeux train/validation/test restent identiques pour les trois modeles.
ATTENTION: le split aleatoire PAR IMAGE ne garantit pas l'independance des enregistrements.
Utiliser --group-by engine ou --group-by parent si pertinent, avec plusieurs groupes par classe.
"""
import argparse
import csv
import html
import json
import os
import random
import re
from collections import Counter
from pathlib import Path

import cv2
import matplotlib.pyplot as plt
import numpy as np
from sklearn.metrics import ConfusionMatrixDisplay, classification_report, confusion_matrix
from sklearn.model_selection import GroupShuffleSplit, train_test_split
from tensorflow import keras
from tensorflow.keras import layers

LABELS = ["good", "broken", "heavyload"]
TIME_STEPS, FREQ_BINS = 128, 64
EXTENSIONS = {".png", ".jpg", ".jpeg", ".bmp", ".webp"}
BASE = Path(__file__).resolve().parent


def extract_label(filename):
    match = re.search(r"(?:^|[_\-.])(good|broken|heavyload)(?=[_\-.]|$)", filename.lower())
    return LABELS.index(match.group(1)) if match else None


def preprocess(path):
    image = cv2.imread(str(path), cv2.IMREAD_GRAYSCALE)
    if image is None:
        raise ValueError(f"Image illisible: {path}")
    image = cv2.resize(image, (TIME_STEPS, FREQ_BINS), interpolation=cv2.INTER_AREA)
    return (image.astype(np.float32) / 255.0).T  # (temps, frequences)


def load_dataset(folder):
    xs, ys, paths = [], [], []
    all_paths = sorted(p for p in Path(folder).rglob("*") if p.is_file() and p.suffix.lower() in EXTENSIONS)
    if not all_paths:
        raise ValueError(f"Aucune image dans {folder}")
    for pos, path in enumerate(all_paths, 1):
        label = extract_label(path.name)
        if label is None:
            print(f"Ignore (etat inconnu): {path.name}")
            continue
        xs.append(preprocess(path))
        ys.append(label)
        paths.append(str(path.resolve()))
        if pos % 50 == 0 or pos == len(all_paths):
            print(f"Chargement: {pos}/{len(all_paths)} fichiers parcourus", flush=True)
    if not xs:
        raise ValueError("Aucun nom de fichier contenant good, broken ou heavyload")
    return np.array(xs, dtype=np.float32), np.array(ys, dtype=np.int64), paths


def get_group(path, mode):
    if mode == "engine":
        match = re.search(r"engine[_-]?(\d+)", Path(path).stem.lower())
        if not match:
            raise ValueError(f"Aucun identifiant engineN dans {path}")
        return "engine" + match.group(1)
    if mode == "parent":
        return str(Path(path).parent)
    return None


def splits(y, paths, group_by):
    inds = np.arange(len(y))
    if group_by == "none":
        for cls in range(len(LABELS)):
            if np.sum(y == cls) < 5:
                raise ValueError("Il faut au moins 5 images par classe pour la separation stratifiée.")
        trainval, test = train_test_split(inds, test_size=0.2, random_state=42, stratify=y)
        train, val = train_test_split(trainval, test_size=0.2, random_state=42, stratify=y[trainval])
        print("ATTENTION : decoupage aleatoire des IMAGES; risque de fuite entre enregistrements.")
    else:
        groups = np.array([get_group(p, group_by) for p in paths])
        for lab in range(len(LABELS)):
            count = len(set(groups[y == lab]))
            if count < 3:
                raise ValueError(f"Classe {LABELS[lab]} : {count} groupes distincts seulement. "
                                 "Impossible de garantir train/val/test independants (minimum 3, "
                                 "davantage recommande). Ajouter des moteurs/enregistrements.")
        gs = GroupShuffleSplit(n_splits=1, test_size=0.2, random_state=42)
        trainval_rel, test_rel = next(gs.split(inds, y, groups))
        trainval, test = inds[trainval_rel], inds[test_rel]
        gs2 = GroupShuffleSplit(n_splits=1, test_size=0.2, random_state=43)
        tr_rel, va_rel = next(gs2.split(trainval, y[trainval], groups[trainval]))
        train, val = trainval[tr_rel], trainval[va_rel]
        for name, ids in [("train", train), ("validation", val), ("test", test)]:
            missing = [LABELS[i] for i in range(len(LABELS)) if i not in y[ids]]
            if missing:
                raise ValueError(f"Split par groupes incomplet: {name} ne contient pas {missing}. "
                                 "Davantage de groupes independants sont necessaires.")
        print("Decoupage par groupes :", group_by, "(aucun groupe partage entre ensembles).")
    for name, idx in [("train", train), ("validation", val), ("test", test)]:
        print(f"  {name}: {len(idx)} images, ", dict(zip(LABELS, np.bincount(y[idx], minlength=3).tolist())))
    return train, val, test


def build_model(kind, lr):
    if kind == "lstm_baseline":
        model = keras.Sequential([
            layers.Input((TIME_STEPS, FREQ_BINS)),
            layers.LSTM(64, return_sequences=True), layers.Dropout(0.3),
            layers.LSTM(32), layers.Dropout(0.3),
            layers.Dense(32, activation="relu"), layers.Dense(3, activation="softmax")
        ])
    elif kind == "lstm":
        model = keras.Sequential([
            layers.Input((TIME_STEPS, FREQ_BINS)),
            layers.LSTM(32), layers.Dropout(0.2),
            layers.Dense(16, activation="relu"), layers.Dense(3, activation="softmax")
        ])
    elif kind == "cnn":
        # CNN : retransformer en 128 x 64 x 1, avec le temps sur l'axe 0.
        model = keras.Sequential([
            layers.Input((TIME_STEPS, FREQ_BINS, 1)),
            layers.Conv2D(16, 3, padding="same", activation="relu"),
            layers.MaxPooling2D(2),
            layers.Conv2D(32, 3, padding="same", activation="relu"),
            layers.MaxPooling2D(2),
            layers.Conv2D(64, 3, padding="same", activation="relu"),
            layers.GlobalAveragePooling2D(), layers.Dense(32, activation="relu"),
            layers.Dropout(0.2), layers.Dense(3, activation="softmax")
        ])
    else:
        raise ValueError(kind)
    model.compile(optimizer=keras.optimizers.Adam(learning_rate=lr),
                  loss="sparse_categorical_crossentropy", metrics=["accuracy"])
    return model


def inputs(X, kind):
    return X[..., None] if kind == "cnn" else X


def plot_history(hist, dest):
    fig, axs = plt.subplots(1, 2, figsize=(12, 4))
    for ax, metric, title in zip(axs, ["accuracy", "loss"], ["Precision", "Perte"]):
        ax.plot(hist.history[metric], label="Train")
        ax.plot(hist.history["val_" + metric], label="Validation")
        ax.set(xlabel="Epoque", ylabel=title, title=title)
        ax.legend()
    fig.tight_layout()
    fig.savefig(dest, dpi=150)
    plt.close(fig)


def save_table(rows, dest):
    columns = ["fichier", "classe_reelle", "classe_predite", "correct", "proba_good", "proba_broken", "proba_heavyload", "confiance"]
    with (dest / "predictions_test.csv").open("w", encoding="utf-8-sig", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=columns, delimiter=";")
        writer.writeheader()
        writer.writerows(rows)
    trs = []
    for r in rows:
        status = "OK" if r["correct"] else "ERREUR"
        cells = [html.escape(str(r[c])) for c in columns]
        trs.append(f'<tr data-status="{status}"><td>{cells[0]}</td><td>{cells[1]}</td><td>{cells[2]}</td>'
                   f'<td class="{"ok" if r["correct"] else "bad"}">{status}</td>' +
                   "".join(f"<td>{v}</td>" for v in cells[4:]) + "</tr>")
    page = """<!doctype html><html lang="fr"><head><meta charset="utf-8"><title>Predictions spectrogrammes</title>
<style>body{font-family:Arial,sans-serif;margin:25px;background:#f7f9fc;color:#202533}
input,select{padding:9px;margin:8px 8px 15px 0}table{border-collapse:collapse;width:100%;background:white}
th,td{padding:9px;border-bottom:1px solid #ddd;text-align:left}thead{position:sticky;top:0;background:#e4ecf7}
.ok{background:#d2f4df;font-weight:bold}.bad{background:#ffd8d8;font-weight:bold}tr:hover{background:#f1f4f9}</style></head>
<body><h2>Predictions sur les images de TEST</h2><p>Probabilites entre 0 et 1. Les images de test n'ont pas servi a l'entrainement.</p>
<input id="search" placeholder="Rechercher fichier ou classe" oninput="filterRows()">
<select id="filter" onchange="filterRows()"><option value="ALL">Tous les resultats</option><option value="ERREUR">Erreurs uniquement</option><option value="OK">Corrects uniquement</option></select>
<p id="count"></p><table><thead><tr>""" + "".join("<th>" + html.escape(c) + "</th>" for c in columns) + """</tr></thead><tbody>""" + "\n".join(trs) + """</tbody></table>
<script>function filterRows(){const q=document.getElementById('search').value.toLowerCase(),f=document.getElementById('filter').value;
let n=0;document.querySelectorAll('tbody tr').forEach(r=>{let ok=r.textContent.toLowerCase().includes(q)&&(f==='ALL'||r.dataset.status===f);r.style.display=ok?'':'none';if(ok)n++});document.getElementById('count').textContent=n+' ligne(s) affichee(s)'}filterRows()</script></body></html>"""
    (dest / "tableau_predictions.html").write_text(page, encoding="utf-8")


def train(args):
    random.seed(args.seed)
    np.random.seed(args.seed)
    keras.utils.set_random_seed(args.seed)
    X, y, paths = load_dataset(args.dataset)
    print("Images par classe:", dict(zip(LABELS, np.bincount(y, minlength=3).tolist())))
    train_idx, val_idx, test_idx = splits(y, paths, args.group_by)
    out = Path(args.output)
    out.mkdir(parents=True, exist_ok=True)
    (out / "classes.json").write_text(json.dumps(LABELS, indent=2), encoding="utf-8")
    with (out / "repartition.csv").open("w", encoding="utf-8-sig", newline="") as f:
        w = csv.writer(f, delimiter=";")
        w.writerow(["fichier", "ensemble", "classe", "groupe"])
        for name, ids in [("train", train_idx), ("validation", val_idx), ("test", test_idx)]:
            for i in ids:
                w.writerow([paths[i], name, LABELS[y[i]], get_group(paths[i], args.group_by)])
    scores = []
    for kind in args.models:
        print(f"\n{'='*60}\nENTRAINEMENT : {kind}\n{'='*60}")
        keras.backend.clear_session()
        keras.utils.set_random_seed(args.seed)
        dest = out / kind
        dest.mkdir(exist_ok=True)
        model = build_model(kind, args.learning_rate)
        model.summary()
        cbs = [keras.callbacks.ModelCheckpoint(str(dest / "meilleur_modele.keras"), monitor="val_loss", save_best_only=True)]
        if kind != "lstm_baseline":
            cbs.extend([
                keras.callbacks.EarlyStopping(monitor="val_loss", patience=15, restore_best_weights=True),
                keras.callbacks.ReduceLROnPlateau(monitor="val_loss", factor=0.5, patience=6, min_lr=1e-6),
            ])
        else:
            cbs.append(keras.callbacks.EarlyStopping(monitor="val_loss", patience=8, restore_best_weights=True))
        fit_X = inputs(X, kind)
        history = model.fit(fit_X[train_idx], y[train_idx],
                            validation_data=(fit_X[val_idx], y[val_idx]),
                            epochs=args.epochs, batch_size=args.batch_size, callbacks=cbs, verbose=1)
        # Toujours evaluer le checkpoint ayant la plus faible val_loss.
        best = keras.models.load_model(str(dest / "meilleur_modele.keras"))
        loss, accuracy = best.evaluate(fit_X[test_idx], y[test_idx], verbose=0)
        probs = best.predict(fit_X[test_idx], verbose=0)
        predicted = np.argmax(probs, axis=1)
        report = classification_report(y[test_idx], predicted, labels=[0, 1, 2],
                                       target_names=LABELS, zero_division=0)
        print(f"\n{kind} : accuracy TEST = {accuracy:.2%}\n{report}")
        (dest / "rapport_classification.txt").write_text(report, encoding="utf-8")
        cm = confusion_matrix(y[test_idx], predicted, labels=[0, 1, 2])
        fig, ax = plt.subplots(figsize=(7, 6))
        ConfusionMatrixDisplay(cm, display_labels=LABELS).plot(ax=ax, cmap="Blues", colorbar=False)
        fig.tight_layout()
        fig.savefig(dest / "matrice_confusion.png", dpi=150)
        plt.close(fig)
        plot_history(history, dest / "courbes_apprentissage.png")
        rows = []
        for pos, i in enumerate(test_idx):
            p = probs[pos]
            rows.append({"fichier": paths[i], "classe_reelle": LABELS[y[i]],
                         "classe_predite": LABELS[predicted[pos]], "correct": int(predicted[pos] == y[i]),
                         "proba_good": f"{p[0]:.4f}", "proba_broken": f"{p[1]:.4f}",
                         "proba_heavyload": f"{p[2]:.4f}", "confiance": f"{p.max():.4f}"})
        save_table(rows, dest)
        good_rows = [r for r in rows if r["classe_reelle"] == "good"]
        print("\nDiagnostic GOOD (probabilites des images reellement good):")
        for r in good_rows:
            print(f"{Path(r['fichier']).name[:55]:55s} => {r['classe_predite']:10s} "
                  f"good={r['proba_good']} broken={r['proba_broken']} heavyload={r['proba_heavyload']}")
        scores.append({"modele": kind, "test_accuracy": f"{accuracy:.4f}", "test_loss": f"{loss:.4f}",
                       "meilleure_val_loss": f"{min(history.history['val_loss']):.4f}",
                       "test_images": len(test_idx), "correctes": int(np.sum(predicted == y[test_idx]))})
    with (out / "comparaison_modeles.csv").open("w", encoding="utf-8-sig", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(scores[0].keys()), delimiter=";")
        w.writeheader()
        w.writerows(scores)
    print("\nCOMPARAISON DES MODELES")
    for row in scores:
        print(f"{row['modele']:16s} test={float(row['test_accuracy']):.2%} ({row['correctes']}/{row['test_images']})")
    print("\nResultats:", out.resolve())
    print("Ouvrir chaque 'tableau_predictions.html' dans un navigateur (filtre des erreurs disponible).")
    print("IMPORTANT: si les images d'un meme enregistrement/moteur sont partagees entre splits, "
          "les scores peuvent etre optimistes.")


def predict(args):
    model_file = Path(args.output) / args.model / "meilleur_modele.keras"
    if not model_file.is_file():
        raise FileNotFoundError(f"Modele introuvable: {model_file}. Entrainer d'abord ce modele.")
    model = keras.models.load_model(str(model_file))
    X = inputs(preprocess(args.predict)[None, ...], args.model)
    p = model.predict(X, verbose=0)[0]
    print(f"Prediction: {LABELS[np.argmax(p)]}")
    for label, prob in zip(LABELS, p):
        print(f"  {label:10s}: {prob:.2%}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dataset", default=str(BASE / "Spectogramme"))
    parser.add_argument("--output", default=str(BASE / "resultats_moteurs"))
    parser.add_argument("--epochs", type=int, default=100)
    parser.add_argument("--batch-size", type=int, default=16)
    parser.add_argument("--learning-rate", type=float, default=1e-4)
    parser.add_argument("--models", nargs="+", choices=["lstm_baseline", "lstm", "cnn"],
                        default=["lstm_baseline", "lstm", "cnn"])
    parser.add_argument("--group-by", choices=["none", "engine", "parent"], default="none")
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--predict", help="Chemin d'une nouvelle image")
    parser.add_argument("--model", choices=["lstm_baseline", "lstm", "cnn"], default="lstm")
    args = parser.parse_args()
    if args.predict:
        predict(args)
    else:
        train(args)
