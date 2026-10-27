"""
CNN Autoencoder Training Script for Mushroom Contamination Visual Anomaly Detection.

Trains a Convolutional Autoencoder on HEALTHY mushroom substrate images.
The network learns the baseline latent representations and reconstruction
of clean mycelium/mushrooms to detect anomalies during inference.
"""

import sys
import os
import argparse
from pathlib import Path
import numpy as np

# Set project root to sys.path
SCRIPT_DIR = Path(__file__).resolve().parent
ML_ROOT = SCRIPT_DIR.parent
if str(ML_ROOT) not in sys.path:
    sys.path.insert(0, str(ML_ROOT))

import tensorflow as tf
from tensorflow.keras import layers, models, callbacks
from preprocessing.image_utils import load_and_preprocess_image, ALLOWED_EXTENSIONS

# Default Hyperparameters
DEFAULT_IMAGE_SIZE = (128, 128)
DEFAULT_BATCH_SIZE = 32
DEFAULT_EPOCHS = 30
DEFAULT_LEARNING_RATE = 1e-3


def build_autoencoder(input_shape=(128, 128, 3)) -> models.Model:
    """
    Constructs the Convolutional Autoencoder architecture.
    
    Encoder compresses 128x128x3 -> 16x16x128 latent representation.
    Decoder reconstructs 16x16x128 -> 128x128x3 with sigmoid activation.
    """
    inputs = layers.Input(shape=input_shape, name="input_image")

    # --- ENCODER ---
    # Block 1: 128x128x3 -> 64x64x32
    x = layers.Conv2D(32, (3, 3), activation="relu", padding="same", name="enc_conv1")(inputs)
    x = layers.MaxPooling2D((2, 2), padding="same", name="enc_pool1")(x)

    # Block 2: 64x64x32 -> 32x32x64
    x = layers.Conv2D(64, (3, 3), activation="relu", padding="same", name="enc_conv2")(x)
    x = layers.MaxPooling2D((2, 2), padding="same", name="enc_pool2")(x)

    # Block 3: 32x32x64 -> 16x16x128 (Latent Bottleneck)
    x = layers.Conv2D(128, (3, 3), activation="relu", padding="same", name="enc_conv3")(x)
    encoded = layers.MaxPooling2D((2, 2), padding="same", name="latent_bottleneck")(x)

    # --- DECODER ---
    # Block 1: 16x16x128 -> 32x32x128
    x = layers.Conv2DTranspose(128, (3, 3), strides=2, activation="relu", padding="same", name="dec_trans1")(encoded)

    # Block 2: 32x32x128 -> 64x64x64
    x = layers.Conv2DTranspose(64, (3, 3), strides=2, activation="relu", padding="same", name="dec_trans2")(x)

    # Block 3: 64x64x64 -> 128x128x32
    x = layers.Conv2DTranspose(32, (3, 3), strides=2, activation="relu", padding="same", name="dec_trans3")(x)

    # Final Reconstruction Layer: 128x128x32 -> 128x128x3 [0.0, 1.0]
    decoded = layers.Conv2D(3, (3, 3), activation="sigmoid", padding="same", name="reconstructed_image")(x)

    autoencoder = models.Model(inputs=inputs, outputs=decoded, name="MushroomCNNAutoencoder")
    return autoencoder


def load_dataset_from_directory(dir_path: Path, target_size=(128, 128)) -> np.ndarray:
    """
    Loads all valid images from a directory into a float32 numpy array.
    """
    if not dir_path.exists():
        return np.empty((0, target_size[0], target_size[1], 3), dtype=np.float32)

    image_files = [
        f for f in dir_path.iterdir()
        if f.is_file() and f.suffix.lower() in ALLOWED_EXTENSIONS
    ]

    if not image_files:
        return np.empty((0, target_size[0], target_size[1], 3), dtype=np.float32)

    images = []
    for file_path in sorted(image_files):
        try:
            arr = load_and_preprocess_image(file_path, target_size=target_size, add_batch_dim=False)
            images.append(arr)
        except Exception as e:
            print(f"Warning: Skipping malformed image '{file_path.name}': {e}")

    if not images:
        return np.empty((0, target_size[0], target_size[1], 3), dtype=np.float32)

    return np.array(images, dtype=np.float32)


def main():
    parser = argparse.ArgumentParser(description="Train CNN Autoencoder for Mushroom Visual Anomaly Detection")
    parser.add_argument("--epochs", type=int, default=DEFAULT_EPOCHS, help="Number of training epochs")
    parser.add_argument("--batch-size", type=int, default=DEFAULT_BATCH_SIZE, help="Mini-batch size")
    parser.add_argument("--lr", type=float, default=DEFAULT_LEARNING_RATE, help="Adam optimizer learning rate")
    parser.add_argument("--train-dir", type=str, default=str(ML_ROOT / "dataset" / "train" / "healthy"), help="Path to healthy training images")
    parser.add_argument("--val-dir", type=str, default=str(ML_ROOT / "dataset" / "validation" / "healthy"), help="Path to healthy validation images")
    parser.add_argument("--output-model", type=str, default=str(ML_ROOT / "model" / "mushroom_autoencoder.keras"), help="Target model destination")
    args = parser.parse_args()

    train_path = Path(args.train_dir)
    val_path = Path(args.val_dir)
    model_output_path = Path(args.output_model)

    print("=" * 70)
    print("Mushroom Cultivation Anomaly Detection: CNN Autoencoder Training")
    print("=" * 70)
    print(f"Training Directory   : {train_path}")
    print(f"Validation Directory : {val_path}")
    print(f"Target Model Save    : {model_output_path}")
    print(f"Hyperparameters      : Epochs={args.epochs}, BatchSize={args.batch_size}, LR={args.lr}")
    print("-" * 70)

    # 1. Load Datasets
    print("Loading training images...")
    X_train = load_dataset_from_directory(train_path, target_size=DEFAULT_IMAGE_SIZE)
    print(f"-> Loaded {len(X_train)} training images.")

    print("Loading validation images...")
    X_val = load_dataset_from_directory(val_path, target_size=DEFAULT_IMAGE_SIZE)
    print(f"-> Loaded {len(X_val)} validation images.")

    if len(X_train) == 0:
        print("\n[ERROR] No training images found in dataset/train/healthy/!")
        print("Please place healthy mushroom substrate images (.jpg, .png) in:")
        print(f"  {train_path}")
        print("\nExample command after adding images:")
        print("  python training/train_autoencoder.py")
        sys.exit(1)

    # If no separate validation set is present, take a 20% validation split from training
    if len(X_val) == 0:
        if len(X_train) >= 5:
            print("Notice: No separate validation images found. Using 20% split of training set for validation.")
            val_split_idx = int(len(X_train) * 0.8)
            indices = np.random.RandomState(42).permutation(len(X_train))
            train_idx, val_idx = indices[:val_split_idx], indices[val_split_idx:]
            X_val = X_train[val_idx]
            X_train = X_train[train_idx]
            print(f"-> Split: {len(X_train)} training, {len(X_val)} validation images.")
        else:
            print("Notice: Dataset too small for separate validation split. Using training data for validation metrics.")
            X_val = X_train

    # 2. Build Model
    print("\nBuilding CNN Autoencoder architecture...")
    model = build_autoencoder(input_shape=(DEFAULT_IMAGE_SIZE[0], DEFAULT_IMAGE_SIZE[1], 3))
    model.compile(
        optimizer=tf.keras.optimizers.Adam(learning_rate=args.lr),
        loss="mean_squared_error",
        metrics=["mae"]
    )
    model.summary()

    # 3. Setup Callbacks
    model_output_path.parent.mkdir(parents=True, exist_ok=True)
    
    cb_list = [
        callbacks.EarlyStopping(
            monitor="val_loss",
            patience=7,
            restore_best_weights=True,
            verbose=1
        ),
        callbacks.ReduceLROnPlateau(
            monitor="val_loss",
            factor=0.5,
            patience=3,
            min_lr=1e-6,
            verbose=1
        )
    ]

    # 4. Train Autoencoder (Input X is reconstructed as Target X)
    print("\nStarting autoencoder training on CPU/GPU...")
    history = model.fit(
        x=X_train,
        y=X_train,
        epochs=args.epochs,
        batch_size=min(args.batch_size, len(X_train)),
        validation_data=(X_val, X_val),
        callbacks=cb_list,
        verbose=1
    )

    # 5. Save Trained Model
    model.save(str(model_output_path))
    print("\n" + "=" * 70)
    print("Training Completed Successfully!")
    print(f"Final Model Saved to : {model_output_path}")
    
    train_loss = history.history["loss"][-1]
    val_loss = history.history["val_loss"][-1]
    print(f"Final Training Loss (MSE)   : {train_loss:.6f}")
    print(f"Final Validation Loss (MSE) : {val_loss:.6f}")
    print("=" * 70)
    print("\nNext Step: Run threshold calculation:")
    print("  python training/calculate_threshold.py")


if __name__ == "__main__":
    main()
