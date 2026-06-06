import pandas as pd
import numpy as np
from sklearn.ensemble import RandomForestRegressor
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import LabelEncoder
from sklearn.metrics import mean_absolute_error, mean_squared_error, r2_score
import joblib

# =============================================
# 1. LOAD DATASET
# =============================================
print("=" * 50)
print("1. LOAD DATASET")
print("=" * 50)
df = pd.read_csv('dataset_sepatu_final.csv')
print(f"Jumlah data: {len(df)} baris")
print(f"Kolom: {df.columns.tolist()}")
print(df.head())
print()

# =============================================
# 2. PREPROCESSING - CEK MISSING VALUE & DUPLIKAT
# =============================================
print("=" * 50)
print("2. PREPROCESSING")
print("=" * 50)
print(f"Missing values:\n{df.isnull().sum()}")
print(f"\nData duplikat: {df.duplicated().sum()}")
print()

# =============================================
# 3. ENCODING FITUR KATEGORIKAL
# =============================================
print("=" * 50)
print("3. ENCODING FITUR KATEGORIKAL")
print("=" * 50)

le_jenis     = LabelEncoder()
le_kebasahan = LabelEncoder()
le_ketebalan = LabelEncoder()

df['jenis_sepatu_enc']       = le_jenis.fit_transform(df['jenis_sepatu'])
df['tingkat_kebasahan_enc']  = le_kebasahan.fit_transform(df['tingkat_kebasahan'])
df['ketebalan_bahan_enc']    = le_ketebalan.fit_transform(df['ketebalan_bahan'])

print("Encoding jenis_sepatu     :", dict(zip(le_jenis.classes_,     le_jenis.transform(le_jenis.classes_))))
print("Encoding tingkat_kebasahan:", dict(zip(le_kebasahan.classes_, le_kebasahan.transform(le_kebasahan.classes_))))
print("Encoding ketebalan_bahan  :", dict(zip(le_ketebalan.classes_, le_ketebalan.transform(le_ketebalan.classes_))))
print()

# Simpan encoder
joblib.dump(le_jenis,     'encoder_jenis.pkl')
joblib.dump(le_kebasahan, 'encoder_kebasahan.pkl')
joblib.dump(le_ketebalan, 'encoder_ketebalan.pkl')
print("Encoder disimpan: encoder_jenis.pkl, encoder_kebasahan.pkl, encoder_ketebalan.pkl")
print()

# =============================================
# 4. FITUR MASUKAN DAN TARGET
# =============================================
print("=" * 50)
print("4. FITUR MASUKAN DAN TARGET")
print("=" * 50)

X = df[['jenis_sepatu_enc', 'suhu', 'kelembapan',
        'tingkat_kebasahan_enc', 'ketebalan_bahan_enc']]
y = df['waktu']

print("Fitur masukan (X):", X.columns.tolist())
print("Variabel target  : waktu (menit)")
print()

# =============================================
# 5. PEMBAGIAN DATA TRAINING DAN TESTING (80:20)
# =============================================
print("=" * 50)
print("5. PEMBAGIAN DATA (80:20)")
print("=" * 50)

X_train, X_test, y_train, y_test = train_test_split(
    X, y, test_size=0.2, random_state=42
)

print(f"Total data  : {len(df)}")
print(f"Data latih  : {len(X_train)} ({len(X_train)/len(df)*100:.0f}%)")
print(f"Data uji    : {len(X_test)} ({len(X_test)/len(df)*100:.0f}%)")
print()

# =============================================
# 6. PELATIHAN MODEL RANDOM FOREST REGRESSOR
# =============================================
print("=" * 50)
print("6. PELATIHAN MODEL")
print("=" * 50)

model = RandomForestRegressor(
    n_estimators=100,
    random_state=42
)
model.fit(X_train, y_train)
print("Model berhasil dilatih!")
print(f"n_estimators : {model.n_estimators}")
print(f"random_state : 42")
print()

# =============================================
# 7. EVALUASI MODEL
# =============================================
print("=" * 50)
print("7. EVALUASI MODEL")
print("=" * 50)

y_pred = model.predict(X_test)

mae  = mean_absolute_error(y_test, y_pred)
mse  = mean_squared_error(y_test, y_pred)
rmse = np.sqrt(mse)
r2   = r2_score(y_test, y_pred)

print(f"MAE  (Mean Absolute Error)       : {mae:.4f} menit")
print(f"MSE  (Mean Squared Error)        : {mse:.4f}")
print(f"RMSE (Root Mean Squared Error)   : {rmse:.4f} menit")
print(f"R²   (Koefisien Determinasi)     : {r2:.4f}")
print()

# =============================================
# 8. SIMPAN MODEL
# =============================================
print("=" * 50)
print("8. SIMPAN MODEL")
print("=" * 50)

joblib.dump(model, 'model_rf.pkl')
print("Model disimpan: model_rf.pkl")

# Export ke C++ header untuk ESP32
try:
    from micromlgen import port
    c_code = port(model)
    with open('model_rf_esp32.h', 'w') as f:
        f.write(c_code)
    print("Model ESP32 disimpan: model_rf_esp32.h")
except Exception as e:
    print(f"Export ESP32 dilewati: {e}")

print()
print("=" * 50)
print("SELESAI")
print("=" * 50)
