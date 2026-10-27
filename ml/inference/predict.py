"""
Inference Module for Mushroom Contamination Visual Anomaly Detection.

Loads the trained CNN Autoencoder and empirical threshold.
Preprocesses input images, computes Mean Squared Reconstruction Error,
and classifies samples as HEALTHY or POSSIBLE_CONTAMINATION.
"""

import sys
import os
import json
from pathlib import Path
from typing import Union, Dict, Any, Optional
import numpy as np

# Set project root to sys.path
SCRIPT_DIR = Path(__file__).resolve().parent
ML_ROOT = SCRIPT_DIR.parent
if str(ML_ROOT) not in sys.path:
    sys.path.insert(0, str(ML_ROOT))

import tensorflow as tf
from preprocessing.image_utils import load_and_preprocess_image, DEFAULT_TARGET_SIZE

# Default Model and Threshold Paths
DEFAULT_MODEL_PATH = ML_ROOT / "model" / "mushroom_autoencoder.keras"
DEFAULT_THRESHOLD_PATH = ML_ROOT / "model" / "threshold.json"


def load_model_and_threshold(
    model_path: Union[str, Path] = DEFAULT_MODEL_PATH,
    threshold_path: Union[str, Path] = DEFAULT_THRESHOLD_PATH
) -> tuple:
    """
    Loads trained Keras autoencoder model and threshold metadata from disk.
    
    Raises:
        FileNotFoundError: If model or threshold.json does not exist.
        RuntimeError: If model file is corrupted or cannot be parsed.
    """
    m_path = Path(model_path)
    t_path = Path(threshold_path)

    if not m_path.exists():
        raise FileNotFoundError(
            f"Trained model not found at '{m_path}'. Please train the model using 'python training/train_autoencoder.py'."
        )

    if not t_path.exists():
        raise FileNotFoundError(
            f"Threshold metadata not found at '{t_path}'. Please calculate threshold using 'python training/calculate_threshold.py'."
        )

    # 1. Load Model
    try:
        model = tf.keras.models.load_model(str(m_path))
    except Exception as e:
        raise RuntimeError(f"Failed to load Keras model from '{m_path}': {str(e)}")

    # 2. Load Threshold
    try:
        with open(t_path, "r", encoding="utf-8") as f:
            threshold_data = json.load(f)
        threshold = float(threshold_data["threshold"])
    except Exception as e:
        raise RuntimeError(f"Failed to parse threshold JSON from '{t_path}': {str(e)}")

    return model, threshold


def calculate_derived_confidence(reconstruction_error: float, threshold: float, detected_class: str) -> float:
    """
    Calculates a deterministic application-level confidence score [50.0, 98.0]
    based on the relative distance between reconstruction error and the cutoff threshold.
    
    Note: This is an empirical distance metric, not a calibrated biological probability.
    """
    if threshold <= 0:
        return 50.0

    if detected_class == "HEALTHY":
        # Distance below threshold: relative safety margin from 0 to 1
        # When error is 0.0 -> margin is 1.0 -> confidence is 95.0%
        # When error is close to threshold -> margin is 0.0 -> confidence is 50.0%
        margin = max(0.0, min(1.0, (threshold - reconstruction_error) / threshold))
        confidence = 50.0 + (margin * 45.0)
    else:
        # Distance above threshold: relative excess over threshold
        # When error slightly exceeds threshold -> excess is small -> confidence is ~52.0%
        # When error is >= 2x threshold -> excess is >= 1.0 -> confidence is capped at 95.0%+
        excess = max(0.0, (reconstruction_error - threshold) / threshold)
        capped_excess = min(1.0, excess)
        confidence = 50.0 + (capped_excess * 45.0)

    return round(float(confidence), 1)


def predict_image(
    image_input: Any,
    model: Optional[tf.keras.Model] = None,
    threshold: Optional[float] = None,
    model_path: Union[str, Path] = DEFAULT_MODEL_PATH,
    threshold_path: Union[str, Path] = DEFAULT_THRESHOLD_PATH
) -> Dict[str, Any]:
    """
    Performs end-to-end inference on a single mushroom image.

    Args:
        image_input: File path, bytes, or PIL Image.
        model: Optional preloaded Keras model (avoids reloading on each request).
        threshold: Optional preloaded threshold value.
        model_path: Path to model if not preloaded.
        threshold_path: Path to threshold.json if not preloaded.

    Returns:
        dict: Standardized prediction JSON matching frontend/Flask contract:
            {
                "detected_class": "HEALTHY" | "POSSIBLE_CONTAMINATION",
                "anomaly_score": float,
                "confidence": float,
                "reconstruction_error": float,
                "threshold": float
            }
    """
    # 1. Ensure Model and Threshold are Loaded
    if model is None or threshold is None:
        loaded_model, loaded_threshold = load_model_and_threshold(model_path, threshold_path)
        if model is None:
            model = loaded_model
        if threshold is None:
            threshold = loaded_threshold

    # 2. Preprocess Image -> Shape (1, 128, 128, 3), range [0.0, 1.0]
    preprocessed_img = load_and_preprocess_image(
        image_source=image_input,
        target_size=DEFAULT_TARGET_SIZE,
        add_batch_dim=True
    )

    # 3. Model Inference (Reconstruction)
    reconstructed_img = model.predict(preprocessed_img, verbose=0)

    # 4. Compute Mean Squared Reconstruction Error
    # Average across H, W, C
    reconstruction_error = float(np.mean(np.square(preprocessed_img - reconstructed_img)))

    # 5. Classify based on Empirical Threshold
    if reconstruction_error <= threshold:
        detected_class = "HEALTHY"
    else:
        detected_class = "POSSIBLE_CONTAMINATION"

    # 6. Compute Normalized Anomaly Score (ratio = error / threshold)
    anomaly_score = float(round(reconstruction_error / threshold, 4)) if threshold > 0 else 0.0

    # 7. Compute Derived Confidence Score (0.0 to 100.0)
    confidence = calculate_derived_confidence(reconstruction_error, threshold, detected_class)

    return {
        "detected_class": detected_class,
        "anomaly_score": round(anomaly_score, 4),
        "confidence": round(confidence, 1),
        "reconstruction_error": round(reconstruction_error, 6),
        "threshold": round(threshold, 6)
    }


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Test Inference on a single mushroom image")
    parser.add_argument("image_path", type=str, help="Path to image file (.jpg, .png)")
    parser.add_argument("--model-path", type=str, default=str(DEFAULT_MODEL_PATH), help="Path to model")
    parser.add_argument("--threshold-path", type=str, default=str(DEFAULT_THRESHOLD_PATH), help="Path to threshold.json")
    args = parser.parse_args()

    try:
        result = predict_image(
            image_input=args.image_path,
            model_path=args.model_path,
            threshold_path=args.threshold_path
        )
        print("\nPrediction Result:")
        print(json.dumps(result, indent=2))
    except Exception as err:
        print(f"\n[ERROR] Inference failed: {err}")
        sys.exit(1)
