"""
FastAPI Microservice for Mushroom Contamination Visual Anomaly Detection.

Exposes REST endpoints:
- GET /health   : Returns service status and model loading state.
- POST /predict : Accepts multipart image upload, returns anomaly detection JSON.
"""

import sys
import os
from pathlib import Path
from contextlib import asynccontextmanager
from typing import Optional

# Set project root to sys.path
SCRIPT_DIR = Path(__file__).resolve().parent
ML_ROOT = SCRIPT_DIR.parent
if str(ML_ROOT) not in sys.path:
    sys.path.insert(0, str(ML_ROOT))

from fastapi import FastAPI, File, UploadFile, HTTPException, status
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse
from dotenv import load_dotenv

import tensorflow as tf
from inference.predict import (
    load_model_and_threshold,
    predict_image,
    DEFAULT_MODEL_PATH,
    DEFAULT_THRESHOLD_PATH
)
from preprocessing.image_utils import validate_image_file, MAX_FILE_SIZE_BYTES

load_dotenv()

# Global in-memory cache for loaded model and threshold
app_state = {
    "model": None,
    "threshold": None,
    "model_loaded": False,
    "threshold_loaded": False,
    "load_error": None
}


def load_artifacts():
    """Attempts to load Keras model and threshold JSON into memory."""
    model_path = os.getenv("MODEL_PATH", str(DEFAULT_MODEL_PATH))
    threshold_path = os.getenv("THRESHOLD_PATH", str(DEFAULT_THRESHOLD_PATH))

    try:
        model, threshold = load_model_and_threshold(model_path, threshold_path)
        app_state["model"] = model
        app_state["threshold"] = threshold
        app_state["model_loaded"] = True
        app_state["threshold_loaded"] = True
        app_state["load_error"] = None
        print(f"-> [ML Service] Successfully loaded model from '{model_path}' and threshold ({threshold:.6f})")
    except Exception as e:
        app_state["model"] = None
        app_state["threshold"] = None
        app_state["model_loaded"] = False
        app_state["threshold_loaded"] = False
        app_state["load_error"] = str(e)
        print(f"-> [ML Service] Notice: Model/threshold not loaded at startup: {e}")


@asynccontextmanager
async def lifespan(app: FastAPI):
    # Startup: Load model once into memory
    load_artifacts()
    yield
    # Shutdown: Clean up resources
    app_state["model"] = None


app = FastAPI(
    title="Mushroom Contamination Visual Anomaly Detection API",
    description="FastAPI service utilizing a TensorFlow/Keras CNN Autoencoder for visual anomaly detection.",
    version="1.0.0",
    lifespan=lifespan
)

# Enable CORS
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.get("/health", status_code=status.HTTP_200_OK)
async def health_check():
    """
    Health check endpoint. Reflects true model & threshold availability.
    """
    if app_state["model_loaded"] and app_state["threshold_loaded"]:
        return {
            "status": "ok",
            "model_loaded": True,
            "threshold_loaded": True
        }
    else:
        return JSONResponse(
            status_code=status.HTTP_200_OK,
            content={
                "status": "error",
                "model_loaded": False,
                "threshold_loaded": False,
                "error": app_state["load_error"] or "Model or threshold not initialized"
            }
        )


@app.post("/predict", status_code=status.HTTP_200_OK)
async def predict_contamination(image: UploadFile = File(...)):
    """
    Inference endpoint for mushroom substrate anomaly detection.
    Accepts multipart/form-data with 'image' file.
    """
    # 1. Check if model and threshold are available
    if not app_state["model_loaded"] or not app_state["threshold_loaded"]:
        # Attempt a reload in case training just completed
        load_artifacts()

    if not app_state["model_loaded"] or not app_state["threshold_loaded"]:
        return JSONResponse(
            status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
            content={
                "error": "ML model or threshold is not available",
                "details": app_state["load_error"] or "Train the model before requesting predictions."
            }
        )

    # 2. Read file bytes from memory
    try:
        contents = await image.read()
    except Exception as e:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail=f"Failed to read uploaded file: {str(e)}"
        )

    # 3. Validate image integrity, extension, and size
    is_valid, error_msg = validate_image_file(
        file_bytes=contents,
        filename=image.filename,
        content_type=image.content_type,
        max_size=MAX_FILE_SIZE_BYTES
    )

    if not is_valid:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail=error_msg
        )

    # 4. Perform Inference
    try:
        result = predict_image(
            image_input=contents,
            model=app_state["model"],
            threshold=app_state["threshold"]
        )
        return result

    except HTTPException:
        raise
    except Exception as e:
        return JSONResponse(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            content={
                "error": "Inference processing failed",
                "details": str(e)
            }
        )


if __name__ == "__main__":
    import uvicorn
    host = os.getenv("HOST", "0.0.0.0")
    port = int(os.getenv("PORT", 8000))
    print(f"Starting FastAPI ML Service on {host}:{port}...")
    uvicorn.run("api.main:app", host=host, port=port, reload=False)
