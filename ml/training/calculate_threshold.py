"""
Threshold Calculation Script for Mushroom Contamination Visual Anomaly Detection.

Computes reconstruction errors on the HEALTHY validation dataset using the
trained CNN Autoencoder. Sets the anomaly threshold at the 95th percentile.
Saves threshold parameters to model/threshold.json.
"""

import sys
import os
import json
import argparse
from datetime import datetime, timezone
from pathlib import Path
import numpy as np

# Set project root to sys.path
SCRIPT_DIR = Path(__file__).resolve().parent
ML_ROOT = SCRIPT_DIR.parent
if str(ML_ROOT) not in sys.path:
    sys.path.insert(0, str(ML_ROOT))

import tensorflow as tf
from preprocessing.image_utils import load_and_preprocess_image, ALLOWED_EXTENSIONS

DEFAULT_IMAGE_SIZE = (128, 128)
DEFAULT_PERCENTILE = 95.0


def compute_reconstruction_errors(model: tf.keras.Model, dataset_dir: Path, target_size=(128, 128)) -> list:
    """
    Computes per-image Mean Squared Reconstruction Error on all valid images in dataset_dir.
    """
    if not dataset_dir.exists():
        return []

    image_files = [
        f for f in dataset_dir.iterdir()
        if f.is_file() and f.suffix.lower() in ALLOWED_EXTENSIONS
    ]

    if not image_files:
        return []

    errors = []
    for file_path in sorted(image_files):
        try:
            # Shape (1, 128, 128, 3)
            img_arr = load_and_preprocess_image(file_path, target_size=target_size, add_batch_dim=True)
            reconstructed = model.predict(img_arr, verbose=0)
            
            # Mean Squared Error across all pixels and channels
            mse = float(np.mean(np.square(img_arr - reconstructed)))
            errors.append(mse)
        except Exception as e:
            print(f"Warning: Failed processing '{file_path.name}': {e}")

    return errors


def main():
    parser = argparse.ArgumentParser(description="Calculate Anomaly Threshold for Mushroom Autoencoder")
    parser.add_argument("--percentile", type=float, default=DEFAULT_PERCENTILE, help="Percentile for anomaly cutoff (default: 95.0)")
    parser.add_argument("--val-dir", type=str, default=str(ML_ROOT / "dataset" / "validation" / "healthy"), help="Path to healthy validation images")
    parser.add_argument("--train-dir", type=str, default=str(ML_ROOT / "dataset" / "train" / "healthy"), help="Fallback healthy training dir")
    parser.add_argument("--model-path", type=str, default=str(ML_ROOT / "model" / "mushroom_autoencoder.keras"), help="Path to trained autoencoder model")
    parser.add_argument("--output-json", type=str, default=str(ML_ROOT / "model" / "threshold.json"), help="Destination JSON path")
    args = parser.parse_args()

    model_path = Path(args.model_path)
    val_path = Path(args.val_dir)
    train_path = Path(args.train_dir)
    output_json_path = Path(args.output_json)

    print("=" * 70)
    print("Mushroom Cultivation: Anomaly Threshold Calculation")
    print("=" * 70)
    print(f"Model Path        : {model_path}")
    print(f"Validation Path   : {val_path}")
    print(f"Cutoff Percentile : {args.percentile}%")
    print(f"Output JSON       : {output_json_path}")
    print("-" * 70)

    # 1. Verify Model Existence
    if not model_path.exists():
        print(f"\n[ERROR] Trained model file not found at: {model_path}")
        print("Please train the autoencoder first before calculating the threshold:")
        print("  python training/train_autoencoder.py")
        sys.exit(1)

    print("Loading trained Keras model...")
    try:
        model = tf.keras.models.load_model(str(model_path))
    except Exception as e:
        print(f"\n[ERROR] Failed to load model from '{model_path}': {e}")
        sys.exit(1)

    # 2. Compute Reconstruction Errors on Healthy Validation Images
    print(f"Calculating reconstruction errors on healthy validation images in '{val_path}'...")
    errors = compute_reconstruction_errors(model, val_path, target_size=DEFAULT_IMAGE_SIZE)

    # If validation set is empty, fallback to healthy training images
    if not errors:
        print(f"Notice: No validation images found in '{val_path}'. Falling back to training images in '{train_path}'...")
        errors = compute_reconstruction_errors(model, train_path, target_size=DEFAULT_IMAGE_SIZE)

    if not errors:
        print("\n[ERROR] No healthy images found in either validation or training folder.")
        print("Cannot calculate empirical threshold without healthy sample images.")
        print("Please add healthy images to dataset/validation/healthy/ or dataset/train/healthy/.")
        sys.exit(1)

    # 3. Statistical Analysis
    errors_arr = np.array(errors, dtype=np.float64)
    threshold_value = float(np.percentile(errors_arr, args.percentile))
    mean_err = float(np.mean(errors_arr))
    std_err = float(np.std(errors_arr))
    min_err = float(np.min(errors_arr))
    max_err = float(np.max(errors_arr))

    threshold_data = {
        "threshold": round(threshold_value, 6),
        "method": f"{args.percentile}th_percentile",
        "validation_samples": len(errors),
        "mean_validation_error": round(mean_err, 6),
        "std_validation_error": round(std_err, 6),
        "min_validation_error": round(min_err, 6),
        "max_validation_error": round(max_err, 6),
        "calculated_at": datetime.now(timezone.utc).astimezone().isoformat()
    }

    # 4. Save to model/threshold.json
    output_json_path.parent.mkdir(parents=True, exist_ok=True)
    with open(output_json_path, "w", encoding="utf-8") as f:
        json.dump(threshold_data, f, indent=2)

    print("\n" + "=" * 70)
    print("Threshold Calculation Completed!")
    print(f"Total Validation Samples Evaluated : {len(errors)}")
    print(f"Mean Reconstruction Error (MSE)   : {mean_err:.6f}")
    print(f"Std Reconstruction Error          : {std_err:.6f}")
    print(f"Min / Max Error Range             : [{min_err:.6f}, {max_err:.6f}]")
    print(f"Determined Anomaly Threshold ({args.percentile}%) : {threshold_value:.6f}")
    print(f"Saved Metadata to                 : {output_json_path}")
    print("=" * 70)
    print("\nNext Step: Evaluate on test set or launch FastAPI service:")
    print("  python evaluation/evaluate_model.py")
    print("  uvicorn api.main:app --host 0.0.0.0 --port 8000")


if __name__ == "__main__":
    main()
