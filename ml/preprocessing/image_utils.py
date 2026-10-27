"""
Image preprocessing utilities for Mushroom Contamination Visual Anomaly Detection.

Handles image loading, format normalization (RGB), resizing to 128x128,
and pixel scaling to [0.0, 1.0].
"""

import io
from pathlib import Path
from typing import Tuple, Union, Optional
import numpy as np
from PIL import Image

# Supported image formats
ALLOWED_EXTENSIONS = {".jpg", ".jpeg", ".png", ".webp"}
ALLOWED_MIME_TYPES = {"image/jpeg", "image/png", "image/webp", "image/jpg"}
DEFAULT_TARGET_SIZE: Tuple[int, int] = (128, 128)
MAX_FILE_SIZE_BYTES = 10 * 1024 * 1024  # 10 MB


def validate_image_file(
    file_bytes: bytes,
    filename: Optional[str] = None,
    content_type: Optional[str] = None,
    max_size: int = MAX_FILE_SIZE_BYTES
) -> Tuple[bool, Optional[str]]:
    """
    Validates uploaded image file bytes, extension, MIME type, and integrity.
    
    Returns:
        (is_valid, error_message): Tuple of boolean and error description if invalid.
    """
    if not file_bytes or len(file_bytes) == 0:
        return False, "Uploaded image file is empty"

    if len(file_bytes) > max_size:
        return False, f"File size ({len(file_bytes) / (1024 * 1024):.1f} MB) exceeds maximum allowed limit of {max_size / (1024 * 1024):.0f} MB"

    if filename:
        ext = Path(filename).suffix.lower()
        if ext not in ALLOWED_EXTENSIONS:
            return False, f"Unsupported file extension '{ext}'. Allowed extensions: {', '.join(sorted(ALLOWED_EXTENSIONS))}"

    if content_type and content_type.lower() not in ALLOWED_MIME_TYPES:
        # Some clients send application/octet-stream; only reject if it's explicitly non-image
        if not content_type.startswith("image/") and content_type != "application/octet-stream":
            return False, f"Unsupported Content-Type '{content_type}'. Expected image/jpeg, image/png, or image/webp"

    # Verify PIL can open and parse image structure
    try:
        with Image.open(io.BytesIO(file_bytes)) as img:
            img.verify()
    except Exception as e:
        return False, f"Corrupted or invalid image file: {str(e)}"

    return True, None


def load_and_preprocess_image(
    image_source: Union[str, Path, bytes, io.BytesIO, Image.Image],
    target_size: Tuple[int, int] = DEFAULT_TARGET_SIZE,
    add_batch_dim: bool = True
) -> np.ndarray:
    """
    Loads an image from a path, raw bytes, or PIL Image, normalizes to RGB,
    resizes to target_size (128x128), and scales pixel values to [0.0, 1.0].

    Args:
        image_source: Path to file, byte buffer, or PIL.Image instance.
        target_size: Target (width, height) tuple, defaults to (128, 128).
        add_batch_dim: If True, returns array with shape (1, H, W, 3).

    Returns:
        np.ndarray: Preprocessed float32 numpy array with values in [0.0, 1.0].
    """
    # 1. Open PIL Image
    if isinstance(image_source, (str, Path)):
        img_path = Path(image_source)
        if not img_path.exists():
            raise FileNotFoundError(f"Image file not found: {img_path}")
        img = Image.open(img_path)
    elif isinstance(image_source, bytes):
        img = Image.open(io.BytesIO(image_source))
    elif isinstance(image_source, io.BytesIO):
        img = Image.open(image_source)
    elif isinstance(image_source, Image.Image):
        img = image_source
    else:
        raise TypeError(f"Unsupported image source type: {type(image_source)}")

    # 2. Convert to RGB (handles RGBA transparency, grayscale, palette modes)
    if img.mode != "RGB":
        img = img.convert("RGB")

    # 3. Resize with high-quality resampling (Lanczos / Bicubic)
    if img.size != target_size:
        img = img.resize(target_size, Image.Resampling.BILINEAR)

    # 4. Convert to NumPy float32 array
    img_array = np.asarray(img, dtype=np.float32)

    # 5. Normalize pixel values from [0, 255] to [0.0, 1.0]
    img_array = img_array / 255.0

    # 6. Add batch dimension if requested -> (1, 128, 128, 3)
    if add_batch_dim:
        img_array = np.expand_dims(img_array, axis=0)

    return img_array


def preprocess_image_array(
    img_array: np.ndarray,
    target_size: Tuple[int, int] = DEFAULT_TARGET_SIZE,
    add_batch_dim: bool = True
) -> np.ndarray:
    """
    Preprocesses a raw numpy array representation of an image.
    
    Args:
        img_array: Array with shape (H, W, 3) or (H, W) or (B, H, W, 3).
        target_size: Target (width, height).
        add_batch_dim: Ensure 4D batch shape.

    Returns:
        np.ndarray: Normalized array in range [0.0, 1.0].
    """
    if not isinstance(img_array, np.ndarray):
        raise TypeError("Input must be a numpy ndarray")

    # Ensure float32
    processed = img_array.astype(np.float32)

    # Scale to [0.0, 1.0] if range is 0-255
    if processed.max() > 1.0:
        processed = processed / 255.0

    if add_batch_dim and processed.ndim == 3:
        processed = np.expand_dims(processed, axis=0)

    return processed
