# ============================================================
# BOT TELEGRAM - PENGERING SEPATU OTOMATIS
# Jalankan: python bot.py
# Tidak perlu Flask - ML langsung di bot ini
# ============================================================

import logging
import joblib
import pandas as pd
from telegram import Update, ReplyKeyboardMarkup, KeyboardButton
from telegram.ext import (
    Application, CommandHandler, MessageHandler,
    filters, ContextTypes
)

# ============================================================
# KONFIGURASI - GANTI TOKEN DI BAWAH INI
# ============================================================
TOKEN = "8660151353:AAHRVk80ZVeM847MOVezLs5Pe7k0JiSk4-I"

# ============================================================
# LOAD MODEL & ENCODER (dari folder C:\ML_Sepatu)
# Pastikan file .pkl ada di folder yang sama dengan bot.py
# Atau ganti path di bawah sesuai lokasi file
# ============================================================
try:
    model             = joblib.load("model_rf.pkl")
    encoder_jenis     = joblib.load("encoder_jenis.pkl")
    encoder_kebasahan = joblib.load("encoder_kebasahan.pkl")
    encoder_ketebalan = joblib.load("encoder_ketebalan.pkl")
    print("✅ Model dan encoder berhasil dimuat")
except Exception as e:
    print(f"❌ Gagal memuat model: {e}")
    print("Pastikan file .pkl ada di folder yang sama dengan bot.py")
    exit()

# Mapping nama tombol ke nama encoder
MAPPING_JENIS = {
    "Kulit" : "Leather",
    "Mesh"  : "Mesh",
    "Kanvas": "Canvas"
}

# Suhu target per jenis sepatu
SUHU_TARGET = {
    "Kulit" : 43.0,
    "Mesh"  : 38.0,
    "Kanvas": 40.0
}

# ============================================================
# FUNGSI PREDIKSI ML
# ============================================================
def prediksi_waktu(jenis: str, suhu: float, kelembapan: float) -> int:
    jenis_en      = MAPPING_JENIS[jenis]
    jenis_enc     = encoder_jenis.transform([jenis_en])[0]
    kebasahan_enc = encoder_kebasahan.transform(["basah"])[0]
    ketebalan_enc = encoder_ketebalan.transform(["sedang"])[0]

    input_data = pd.DataFrame([{
        "jenis_sepatu_enc"     : jenis_enc,
        "suhu"                 : suhu,
        "kelembapan"           : kelembapan,
        "tingkat_kebasahan_enc": kebasahan_enc,
        "ketebalan_bahan_enc"  : ketebalan_enc
    }])

    hasil = model.predict(input_data)[0]
    return int(hasil)

# ============================================================
# KEYBOARD TOMBOL
# ============================================================
def get_keyboard():
    keyboard = [
        [KeyboardButton("👟 Mesh"),    KeyboardButton("🧵 Kanvas")],
        [KeyboardButton("👢 Kulit"),   KeyboardButton("📊 Status")]
    ]
    return ReplyKeyboardMarkup(keyboard, resize_keyboard=True)

# ============================================================
# HANDLER /start
# ============================================================
async def start(update: Update, context: ContextTypes.DEFAULT_TYPE):
    await update.message.reply_text(
        "👋 *Selamat datang di SMART SHOE DRYER!*\n\n"
        "Silakan pilih jenis sepatu:\n\n"
        "👟 *Mesh* — Sepatu kain/rajut\n"
        "🧵 *Kanvas* — Sepatu kanvas\n"
        "👢 *Kulit* — Sepatu kulit\n"
        "📊 *Status* — Lihat status pengering",
        parse_mode="Markdown",
        reply_markup=get_keyboard()
    )

# ============================================================
# HANDLER PESAN
# ============================================================
async def handle_message(update: Update, context: ContextTypes.DEFAULT_TYPE):
    text = update.message.text.strip()

    # Bersihkan emoji dari teks tombol
    jenis = None
    if "Mesh"   in text: jenis = "Mesh"
    if "Kanvas" in text: jenis = "Kanvas"
    if "Kulit"  in text: jenis = "Kulit"
    if "Status" in text:
        await update.message.reply_text(
            "📊 *Status Pengering*\n\n"
            "🟢 Bot aktif dan siap\n"
            "🤖 Model ML: Random Forest\n"
            "📡 Mode: Standalone (tanpa Flask)",
            parse_mode="Markdown",
            reply_markup=get_keyboard()
        )
        return

    if jenis is None:
        await update.message.reply_text(
            "Silakan pilih jenis sepatu menggunakan tombol di bawah.",
            reply_markup=get_keyboard()
        )
        return

    # Simpan jenis ke context untuk menunggu data sensor
    context.user_data["jenis"] = jenis

    # Untuk demo: pakai nilai default sensor
    # Nanti bisa diganti dengan data real dari ESP32
    suhu       = 30.0   # suhu ruangan default
    kelembapan = 80.0   # kelembapan default

    await update.message.reply_text(
        f"🔄 Menghitung prediksi waktu...",
        reply_markup=get_keyboard()
    )

    try:
        waktu = prediksi_waktu(jenis, suhu, kelembapan)
        suhu_target = SUHU_TARGET[jenis]

        await update.message.reply_text(
            f"✅ *PENGERING AKTIF!*\n"
            f"{'─'*25}\n"
            f"👟 Jenis    : {jenis}\n"
            f"🌡️ Target   : {suhu_target} °C\n"
            f"🔮 Prediksi : {waktu} menit\n"
            f"{'─'*25}\n"
            f"🔥 Pengering dimulai!",
            parse_mode="Markdown",
            reply_markup=get_keyboard()
        )

    except Exception as e:
        await update.message.reply_text(
            f"❌ Error prediksi: {str(e)}\n"
            "Cek apakah file model .pkl tersedia.",
            reply_markup=get_keyboard()
        )

# ============================================================
# MAIN - JALANKAN BOT
# ============================================================
logging.basicConfig(
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s",
    level=logging.INFO
)

def main():
    print("=" * 45)
    print("  BOT TELEGRAM PENGERING SEPATU AKTIF")
    print("=" * 45)
    print("  Tekan Ctrl+C untuk berhenti")
    print("=" * 45)

    app = Application.builder().token(TOKEN).build()
    app.add_handler(CommandHandler("start", start))
    app.add_handler(MessageHandler(filters.TEXT & ~filters.COMMAND, handle_message))
    app.run_polling()

if __name__ == "__main__":
    main()
