from flask import Flask, request, jsonify
import os
import pandas as pd
import joblib

app = Flask(__name__)
model         = joblib.load("model_linear_v4.pkl")
encoder_jenis = joblib.load("encoder_jenis_v4.pkl")

# ======================================================
# RENTANG DATA TRAINING - dari 9 titik pengujian riil
# ======================================================
# Model ini regresi linear dilatih pada 9 titik (n=1 per kombinasi
# jenis x kebasahan, BELUM direplikasi 3x sesuai protokol). Di luar
# rentang ini, prediksi adalah EKSTRAPOLASI dan sudah terbukti bisa
# menghasilkan angka tidak masuk akal (pernah diuji: -359 menit untuk
# input di luar rentang). Endpoint ini memberi flag "ekstrapolasi"
# di response supaya firmware/pengguna tahu kapan harus curiga.
SUHU_AWAL_MIN, SUHU_AWAL_MAX = 29.0, 34.0
KELEMBAPAN_AWAL_MIN, KELEMBAPAN_AWAL_MAX = 60.0, 100.0

@app.route('/prediksi', methods=['POST'])
def prediksi():
    data = request.json

    jenis           = data["jenis_sepatu"]
    suhu_awal       = data["suhu_awal"]
    kelembapan_awal = data["kelembapan_awal"]

    mapping = {"Kanvas": "Canvas", "Kulit": "Leather", "Mesh": "Mesh"}
    jenis = mapping.get(jenis, jenis)

    try:
        jenis_enc = encoder_jenis.transform([jenis])[0]
    except ValueError:
        return jsonify({"error": f"jenis_sepatu '{jenis}' tidak dikenal"}), 400

    ekstrapolasi = not (SUHU_AWAL_MIN <= suhu_awal <= SUHU_AWAL_MAX) \
                or not (KELEMBAPAN_AWAL_MIN <= kelembapan_awal <= KELEMBAPAN_AWAL_MAX)

    input_data = pd.DataFrame([{
        "jenis_enc":       jenis_enc,
        "suhu_awal":       suhu_awal,
        "kelembapan_awal": kelembapan_awal
    }])

    hasil = model.predict(input_data)[0]

    # Batas keamanan kasar: kalau hasil ekstrapolasi jadi negatif atau
    # tidak masuk akal (>360 menit / <10 menit), jangan kirim mentah2
    # -- pakai fallback dan tandai jelas.
    fallback_dipakai = False
    if hasil < 10 or hasil > 360:
        hasil = 180  # fallback kasar, BUKAN prediksi bermakna
        fallback_dipakai = True

    return jsonify({
        "prediksi_waktu": int(hasil),
        "ekstrapolasi": ekstrapolasi,
        "fallback_dipakai": fallback_dipakai
    })

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=int(os.environ.get("PORT", 5000)))
