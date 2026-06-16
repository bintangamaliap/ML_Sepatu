from flask import Flask, request, jsonify
import os
import pandas as pd
import joblib

app = Flask(__name__)
model         = joblib.load("model_rf.pkl")
encoder_jenis = joblib.load("encoder_jenis.pkl")

@app.route('/prediksi', methods=['POST'])
def prediksi():
    data = request.json

    # Ambil field baru
    jenis            = data["jenis_sepatu"]
    suhu_awal        = data["suhu_awal"]
    kelembapan_awal  = data["kelembapan_awal"]
    suhu_akhir       = data["suhu_akhir"]
    kelembapan_akhir = data["kelembapan_akhir"]

    # Mapping nama jenis (jaga-jaga jika ESP32 kirim nama Indonesia)
    mapping = {
        "Kanvas": "Canvas",
        "Kulit":  "Leather",
        "Mesh":   "Mesh"
    }
    jenis = mapping.get(jenis, jenis)

    jenis_enc = encoder_jenis.transform([jenis])[0]

    input_data = pd.DataFrame([{
        "jenis_sepatu_enc":  jenis_enc,
        "suhu_awal":         suhu_awal,
        "kelembapan_awal":   kelembapan_awal,
        "suhu_akhir":        suhu_akhir,
        "kelembapan_akhir":  kelembapan_akhir
    }])

    hasil = model.predict(input_data)[0]
    return jsonify({"prediksi_waktu": int(hasil)})

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=int(os.environ.get("PORT", 5000)))
