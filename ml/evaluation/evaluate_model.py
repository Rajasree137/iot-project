"""
Model Evaluation Script for Mushroom Contamination Visual Anomaly Detection.

Evaluates the trained CNN Autoencoder and threshold on the TEST dataset:
- dataset/test/healthy/ (Class: Healthy / Negative)
- dataset/test/contaminated/ (Class: Anomaly / Positive)

Computes:
- Accuracy, Precision, Recall, F1-Score
- Confusion Matrix [[TN, FP], [FN, TP]]
- Reconstruction error distributions for healthy vs. contaminated classes
"""

import sys
import os
import json
import argparse
from pathlib import Path
from typing import List, Tuple
import numpy as np

# Set project root to sys.path
SCRIPT_DIR = Path(__file__).resolve().parent
ML_ROOT = SCRIPT_DIR.parent
if str(ML_ROOT) not in sys.path:
    sys.path.insert(0, str(ML_ROOT))

import tensorflow as tf
from inference.predict import load_model_and_threshold, predict_image
from preprocessing.image_utils import ALLOWED_EXTENSIONS

DEFAULT_TEST_HEALTHY = ML_ROOT / "dataset" / "test" / "healthy"
DEFAULT_TEST_CONTAMINATED = ML_ROOT / "dataset" / "test" / "contaminated"
DEFAULT_MODEL_PATH = ML_ROOT / "model" / "mushroom_autoencoder.keras"
DEFAULT_THRESHOLD_PATH = ML_ROOT / "model" / "threshold.json"


def evaluate_folder(
    folder_path: Path,
    expected_class: str,
    model: tf.keras.Model,
    threshold: float
) -> Tuple[List[float], List[str]]:
    """
    Runs predictions on all images in a test folder.
    Returns (list of reconstruction errors, list of predicted labels).
    """
    if not folder_path.exists():
        return [], []

    image_files = [
        f for f in folder_path.iterdir()
        if f.is_file() and f.suffix.lower() in ALLOWED_EXTENSIONS
    ]

    errors = []
    predictions = []

    for img_path in sorted(image_files):
        try:
            res = predict_image(
                image_input=img_path,
                model=model,
                threshold=threshold
            )
            errors.append(res["reconstruction_error"])
            predictions.append(res["detected_class"])
        except Exception as e:
            print(f"Warning: Failed to evaluate '{img_path.name}': {e}")

    return errors, predictions


def main():
    parser = argparse.ArgumentParser(description="Evaluate Mushroom Anomaly Detection Model on Test Set")
    parser.add_argument("--test-healthy", type=str, default=str(DEFAULT_TEST_HEALTHY), help="Path to healthy test images")
    parser.add_argument("--test-contaminated", type=str, default=str(DEFAULT_TEST_CONTAMINATED), help="Path to contaminated test images")
    parser.add_argument("--model-path", type=str, default=str(DEFAULT_MODEL_PATH), help="Path to model file")
    parser.add_argument("--threshold-path", type=str, default=str(DEFAULT_THRESHOLD_PATH), help="Path to threshold.json")
    args = parser.parse_args()

    healthy_dir = Path(args.test_healthy)
    contam_dir = Path(args.test_contaminated)
    model_path = Path(args.model_path)
    threshold_path = Path(args.threshold_path)

    print("=" * 70)
    print("Mushroom Cultivation: Model Test Set Evaluation")
    print("=" * 70)
    print(f"Model Path               : {model_path}")
    print(f"Threshold Path           : {threshold_path}")
    print(f"Test Healthy Folder      : {healthy_dir}")
    print(f"Test Contaminated Folder : {contam_dir}")
    print("-" * 70)

    # 1. Load Model and Threshold
    try:
        model, threshold = load_model_and_threshold(model_path, threshold_path)
        print(f"Loaded model and threshold ({threshold:.6f}) successfully.\n")
    except Exception as e:
        print(f"[ERROR] Failed to load model or threshold: {e}")
        sys.exit(1)

    # 2. Evaluate Healthy Test Images (Expected: HEALTHY)
    healthy_errors, healthy_preds = evaluate_folder(healthy_dir, "HEALTHY", model, threshold)
    
    # 3. Evaluate Contaminated Test Images (Expected: POSSIBLE_CONTAMINATION)
    contam_errors, contam_preds = evaluate_folder(contam_dir, "POSSIBLE_CONTAMINATION", model, threshold)

    n_healthy = len(healthy_errors)
    n_contam = len(contam_errors)
    total_test = n_healthy + n_contam

    if total_test == 0:
        print("[ERROR] No test images found in test directories:")
        print(f"  Healthy      : {healthy_dir}")
        print(f"  Contaminated : {contam_dir}")
        print("\nPlease add test images to these folders to compute evaluation metrics.")
        sys.exit(1)

    # 4. Compute Confusion Matrix Elements
    # Healthy is Negative (0), Contaminated/Anomaly is Positive (1)
    # TN: Healthy predicted as HEALTHY
    # FP: Healthy predicted as POSSIBLE_CONTAMINATION
    # TP: Contaminated predicted as POSSIBLE_CONTAMINATION
    # FN: Contaminated predicted as HEALTHY
    tn = sum(1 for p in healthy_preds if p == "HEALTHY")
    fp = sum(1 for p in healthy_preds if p == "POSSIBLE_CONTAMINATION")
    tp = sum(1 for p in contam_preds if p == "POSSIBLE_CONTAMINATION")
    fn = sum(1 for p in contam_preds if p == "HEALTHY")

    # Metrics
    accuracy = (tp + tn) / total_test if total_test > 0 else 0.0
    precision = tp / (tp + fp) if (tp + fp) > 0 else 0.0
    recall = tp / (tp + fn) if (tp + fn) > 0 else 0.0
    f1 = (2 * precision * recall) / (precision + recall) if (precision + recall) > 0 else 0.0

    print("=" * 70)
    print("EVALUATION RESULTS & METRICS")
    print("=" * 70)
    print(f"Total Test Samples Evaluated : {total_test}")
    print(f"  - Healthy Samples          : {n_healthy}")
    print(f"  - Contaminated Samples     : {n_contam}")
    print("-" * 70)
    print(f"Accuracy  : {accuracy * 100:.2f}% ({tp + tn}/{total_test})")
    print(f"Precision : {precision * 100:.2f}% (TP / [TP + FP])")
    print(f"Recall    : {recall * 100:.2f}% (TP / [TP + FN])")
    print(f"F1 Score  : {f1 * 100:.2f}%")
    print("-" * 70)
    print("CONFUSION MATRIX:")
    print("                  Predicted HEALTHY    Predicted CONTAMINATED")
    print(f"Actual HEALTHY        TN = {tn:<15} FP = {fp:<15}")
    print(f"Actual CONTAMINATED   FN = {fn:<15} TP = {tp:<15}")
    print("-" * 70)

    # 5. Reconstruction Error Statistics
    if healthy_errors:
        h_arr = np.array(healthy_errors)
        print(f"Healthy Images MSE      : Mean = {np.mean(h_arr):.6f}, Std = {np.std(h_arr):.6f}, Max = {np.max(h_arr):.6f}")
    if contam_errors:
        c_arr = np.array(contam_errors)
        print(f"Contaminated Images MSE : Mean = {np.mean(c_arr):.6f}, Std = {np.std(c_arr):.6f}, Min = {np.min(c_arr):.6f}")
    
    print(f"Decision Threshold      : {threshold:.6f}")
    print("=" * 70)


if __name__ == "__main__":
    main()
