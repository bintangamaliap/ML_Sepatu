from flask import Flask, request, jsonify

import pandas as pd
import joblib
import os

app = Flask(__name__)

model = joblib.load("model_rf_v5.pkl")
encoder = joblib.load("encoder_jenis_v5.pkl")

REQUIRED_FIELDS = [
    "jenis_sepatu",
    "suhu_awal",
    "kelembapan_awal"
]

@app.route("/prediksi", methods=["POST"])

def prediksi():

    data = request.get_json()

    if data is None:
        return jsonify({
            "error":"JSON kosong"
        }),400

    missing = [x for x in REQUIRED_FIELDS if x not in data]

    if missing:

        return jsonify({
            "error":f"Field hilang {missing}"
        }),400

    try:

        jenis = data["jenis_sepatu"]

        suhu = float(data["suhu_awal"])

        rh = float(data["kelembapan_awal"])

    except:

        return jsonify({
            "error":"Format data salah"
        }),400

    mapping = {

        "Kanvas":"Canvas",
        "Canvas":"Canvas",

        "Kulit":"Leather",
        "Leather":"Leather",

        "Mesh":"Mesh"

    }

    jenis = mapping.get(jenis,jenis)

    try:

        jenis_enc = encoder.transform([jenis])[0]

    except:

        return jsonify({
            "error":"Jenis sepatu tidak dikenal"
        }),400

    input_data = pd.DataFrame([{

        "jenis_enc":jenis_enc,
        "suhu_awal":suhu,
        "kelembapan_awal":rh

    }])

    hasil = model.predict(input_data)[0]

    return jsonify({

        "prediksi_waktu":int(round(hasil))

    })

if __name__=="__main__":

    app.run(

        host="0.0.0.0",

        port=int(os.environ.get("PORT",5000))

    )