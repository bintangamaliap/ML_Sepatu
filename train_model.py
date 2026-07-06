import pandas as pd
import joblib
import numpy as np

from sklearn.preprocessing import LabelEncoder
from sklearn.ensemble import RandomForestRegressor
from sklearn.model_selection import train_test_split
from sklearn.metrics import mean_absolute_error, mean_squared_error, r2_score

# ==========================
# Baca Dataset
# ==========================
df = pd.read_excel("Dataset_Pengujian_Sepatu.xlsx")

# ==========================
# Encoder
# ==========================
encoder_jenis = LabelEncoder()

df["jenis_enc"] = encoder_jenis.fit_transform(df["jenis_sepatu"])

# ==========================
# Feature
# ==========================
X = df[[
    "jenis_enc",
    "suhu_awal",
    "kelembapan_awal"
]]

# ==========================
# Target
# ==========================
y = df["target_kering_menit"]

# ==========================
# Split Data
# ==========================
X_train, X_test, y_train, y_test = train_test_split(
    X,
    y,
    test_size=0.2,
    random_state=42
)

# ==========================
# Random Forest
# ==========================
model = RandomForestRegressor(
    n_estimators=200,
    random_state=42
)

model.fit(X_train, y_train)

# ==========================
# Evaluasi
# ==========================
pred = model.predict(X_test)

mae = mean_absolute_error(y_test, pred)
rmse = np.sqrt(mean_squared_error(y_test, pred))
r2 = r2_score(y_test, pred)

print("================================")
print("MAE  :", round(mae,2))
print("RMSE :", round(rmse,2))
print("R2   :", round(r2,4))
print("================================")

# ==========================
# Simpan Model
# ==========================
joblib.dump(model, "model_rf_v5.pkl")
joblib.dump(encoder_jenis, "encoder_jenis_v5.pkl")

print("Model berhasil disimpan!")