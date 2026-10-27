# Module 3: ML/AI Service — CNN Autoencoder for Mushroom Visual Anomaly Detection

Final-Year B.E. Electronics and Communication Engineering Project  
**"IoT and AI Based Smart Mushroom Cultivation System for Contamination Detection and Yield Prediction"**

---

## 1. System Architecture & End-to-End Pipeline

```
[ ESP32 Sensor Hub ] ──(WiFi / HTTP)──> [ Flask Backend :5000 ] <──(REST/SSE)──> [ React Dashboard :5173 ]
                                                │
                                    (POST /api/predict)
                                                ▼
                                    [ FastAPI ML Service :8000 ]
                                                │
                                                ▼
                                    [ Preprocessing 128x128x3 ]
                                                │
                                                ▼
                                    [ CNN Autoencoder Model ]
                                                │
                                                ▼
                                    [ Reconstructed Image ]
                                                │
                                                ▼
                                    [ MSE Reconstruction Error ]
                                                │
                                                ▼
                                  [ Compare vs. 95th % Threshold ]
                                                │
                                 ┌──────────────┴──────────────┐
                                 ▼                             ▼
                            [ HEALTHY ]             [ POSSIBLE_CONTAMINATION ]
```

---

## 2. Scientific Methodology & Viva Defense Summary

### Anomaly Detection via Unsupervised Reconstruction
Unlike supervised multi-class classification which requires thousands of labeled images for every possible mushroom disease/mold, this system employs **one-class visual anomaly detection using a Convolutional Autoencoder (CNN Autoencoder)**.

1. **Learning Normalcy**: The network is trained exclusively on **healthy** mushroom substrate and mycelium images. It learns efficient low-dimensional bottleneck representations (16×16×128) of normal texture, color, and structure.
2. **Reconstruction Divergence**: When presented with an unseen healthy image, the autoencoder reconstructs it accurately with low Mean Squared Error ($MSE \le \text{Threshold}$).
3. **Anomaly Identification**: When presented with a contaminated or anomalous substrate (e.g., green mold, discoloration, abnormal spots), the autoencoder fails to accurately reconstruct the anomalous visual features, resulting in elevated reconstruction error ($MSE > \text{Threshold}$).

> [!IMPORTANT]
> **Scientific Limitation Statement (For Project Viva / Evaluation):**  
> The CNN autoencoder performs **visual anomaly detection** by learning the reconstruction characteristics of healthy mushroom images. An image classified as `POSSIBLE_CONTAMINATION` indicates that its visual pattern differs from the learned healthy distribution. It does **not** prove the presence of a specific biological or bacterial contaminant. The `anomaly_score` and `confidence` are application-level derived metrics based on distance from the empirical threshold, and should not be interpreted as calibrated biological probabilities.

---

## 3. Directory Structure

```
ml/
├── dataset/
│   ├── train/
│   │   └── healthy/           # Healthy images for training
│   ├── validation/
│   │   └── healthy/           # Healthy images for validation
│   └── test/
│       ├── healthy/           # Healthy test images (evaluation)
│       └── contaminated/      # Contaminated / anomaly test images
│
├── model/
│   ├── mushroom_autoencoder.keras  # Saved Keras model file
│   └── threshold.json              # Empirical 95th percentile metadata
│
├── training/
│   ├── train_autoencoder.py   # Model architecture & training script
│   └── calculate_threshold.py # 95th percentile threshold calculator
│
├── evaluation/
│   └── evaluate_model.py      # Computes Accuracy, F1, Confusion Matrix
│
├── inference/
│   └── predict.py             # Reusable inference function & confidence logic
│
├── preprocessing/
│   └── image_utils.py         # Image validation, RGB conversion, 128x128 resize
│
├── api/
│   └── main.py                # FastAPI REST service
│
├── requirements.txt           # CPU-compatible Python dependencies
├── .env.example               # Environment variables template
└── README.md                  # Comprehensive guide
```

---

## 4. Step-by-Step Setup & Execution

### Step 1 — Navigate to ML Directory
```bash
cd ml
```

### Step 2 — Create Virtual Environment
```bash
python3 -m venv venv
```

### Step 3 — Activate Virtual Environment
- **macOS / Linux**:
  ```bash
  source venv/bin/activate
  ```
- **Windows**:
  ```cmd
  venv\Scripts\activate
  ```

### Step 4 — Install Dependencies
```bash
pip install -r requirements.txt
```

### Step 5 — Add Dataset Images
Place your mushroom substrate photos into the appropriate folders:
- `dataset/train/healthy/`: Clean, healthy mushroom/substrate photos for training.
- `dataset/validation/healthy/`: Clean mushroom photos for validation.
- `dataset/test/healthy/`: Clean mushroom test images.
- `dataset/test/contaminated/`: Discolored / moldy / anomalous test images.

### Step 6 — Train the CNN Autoencoder
```bash
python training/train_autoencoder.py
```
*Options:*
`--epochs 30` | `--batch-size 32` | `--lr 0.001`

### Step 7 — Calculate the Empirical Threshold
Calculates reconstruction errors on the healthy validation set and sets the 95th percentile cutoff:
```bash
python training/calculate_threshold.py
```
*Saves output to `model/threshold.json`.*

### Step 8 — Evaluate Model on Test Dataset
```bash
python evaluation/evaluate_model.py
```
*Outputs Accuracy, Precision, Recall, F1 Score, Confusion Matrix, and Error distributions.*

### Step 9 — Start FastAPI Microservice
```bash
uvicorn api.main:app --host 0.0.0.0 --port 8000
```

---

## 5. API Reference & Contract

### 1. Health Check: `GET /health`
```bash
curl http://localhost:8000/health
```
**Healthy Response:**
```json
{
  "status": "ok",
  "model_loaded": true,
  "threshold_loaded": true
}
```
**Model Not Yet Trained Response:**
```json
{
  "status": "error",
  "model_loaded": false,
  "threshold_loaded": false,
  "error": "Trained model not found at 'model/mushroom_autoencoder.keras'"
}
```

---

### 2. Predict Contamination: `POST /predict`
```bash
curl -X POST http://localhost:8000/predict \
  -F "image=@dataset/test/healthy/sample1.jpg"
```

**Healthy Output:**
```json
{
  "detected_class": "HEALTHY",
  "anomaly_score": 0.4215,
  "confidence": 82.4,
  "reconstruction_error": 0.001897,
  "threshold": 0.004501
}
```

**Contaminated Output:**
```json
{
  "detected_class": "POSSIBLE_CONTAMINATION",
  "anomaly_score": 1.5420,
  "confidence": 88.5,
  "reconstruction_error": 0.006941,
  "threshold": 0.004501
}
```

---

## 6. Metric Definitions for Viva

| Metric | Calculation / Definition |
|---|---|
| **Reconstruction Error ($MSE$)** | $\frac{1}{H \times W \times C} \sum (x_{i,j,c} - \hat{x}_{i,j,c})^2$ across all $128 \times 128 \times 3$ normalized pixel channels. |
| **Threshold ($T$)** | 95th percentile of MSE reconstruction errors on the uncorrupted validation dataset. |
| **Anomaly Score** | $\frac{MSE}{T}$ (Values $\le 1.0$ indicate healthy; values $> 1.0$ indicate visual divergence). |
| **Confidence (%)** | Deterministic function of the distance $|MSE - T| / T$, bounded between $50.0\%$ and $98.0\%$. |
