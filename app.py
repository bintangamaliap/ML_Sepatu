from flask import Flask, request, jsonify
import os
import pandas as pd
import joblib

app = Flask(__name__)
model             = joblib.load("model_rf.pkl")
encoder_jenis     = joblib.load("encoder_jenis.pkl")
encoder_kebasahan = joblib.load("encoder_kebasahan.pkl")
encoder_ketebalan = joblib.load("encoder_ketebalan.pkl")

@app.route('/prediksi', methods=['POST'])
def prediksi():
    data       = request.json
    jenis      = data["jenis_sepatu"]
    suhu       = data["suhu"]
    kelembapan = data["kelembapan"]

    # ESP32 sudah kirim nama Inggris, tapi jaga-jaga
    mapping = {
        "Kanvas": "Canvas",
        "Kulit":  "Leather",
        "Mesh":   "Mesh"
    }
    jenis = mapping.get(jenis, jenis)

    jenis_enc     = encoder_jenis.transform([jenis])[0]
    kebasahan_enc = encoder_kebasahan.transform(["basah"])[0]  # default
    ketebalan_enc = encoder_ketebalan.transform(["sedang"])[0]  # default

    input_data = pd.DataFrame([{
        "jenis_sepatu_enc":       jenis_enc,
        "suhu":                   suhu,
        "kelembapan":             kelembapan,
        "tingkat_kebasahan_enc":  kebasahan_enc,
        "ketebalan_bahan_enc":    ketebalan_enc
    }])

    hasil = model.predict(input_data)[0]
    return jsonify({"prediksi_waktu": int(hasil)})

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=int(os.environ.get("PORT", 5000)))s