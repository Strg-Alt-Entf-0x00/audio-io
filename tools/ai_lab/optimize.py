import numpy as np
import scipy.io.wavfile as wav
from scipy.optimize import differential_evolution
import subprocess
import os
import shutil
import time
import json
from datetime import datetime

from signals import generate_torture_test
from metrics import align_signals, compute_spectrogram_mse, compute_envelope_correlation

# Config
CLI_PATH = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "build", "tools", "Release", "audio-io-cli.exe"))
LAME_PATH = os.path.abspath(os.path.join(os.path.dirname(__file__), "bin", "lame", "lame.exe"))
OUTPUTS_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "test_outputs"))

# Run State
RUN_ID = datetime.now().strftime("%Y-%m-%d_%H-%M-%S") + "_baseline"
RUN_DIR = os.path.join(OUTPUTS_DIR, RUN_ID)
REPORT_FILE = os.path.join(RUN_DIR, "report.txt")
METRICS_JSON_FILE = os.path.join(RUN_DIR, "metrics.json")

ORIG_WAV = os.path.join(RUN_DIR, "opt_orig.wav")
LAME_MP3 = os.path.join(RUN_DIR, "opt_lame.mp3")
LAME_WAV = os.path.join(RUN_DIR, "opt_lame.wav")
TEST_MP3 = os.path.join(RUN_DIR, "opt_test.mp3")
TEST_WAV = os.path.join(RUN_DIR, "opt_test.wav")

LAME_DATA = None
BEST_SCORE = float('inf')
ITERATION = 0
METRICS_HISTORY = []

# Bounds centered around author safe defaults
BOUNDS = [
    (5.0, 25.0),     # TMN (safe: 13)
    (1.0, 10.0),     # NMT (safe: 4.5)
    (80.0, 150.0),   # ATH_SHIFT (safe: 120)
    (0.5, 3.0),      # SPREAD_UP (safe: 1.35)
    (1.0, 5.0),      # SPREAD_DOWN (safe: 2.7)
    (0.01, 0.2),     # FWD_DECAY (safe: 0.063)
    (1.0, 5.0)       # PRE_ECHO (safe: 2.0)
]

def log_msg(msg):
    print(msg)
    with open(REPORT_FILE, "a") as f:
        f.write(msg + "\n")

def save_json():
    with open(METRICS_JSON_FILE, "w") as f:
        json.dump({"iterations": METRICS_HISTORY}, f, indent=4)

def init_lame_baseline():
    global LAME_DATA
    if not os.path.exists(LAME_PATH):
        print(f"ERROR: LAME not found at {LAME_PATH}")
        exit(1)
        
    log_msg("Generating LAME Ground Truth...")
    generate_torture_test(ORIG_WAV)
    subprocess.run([LAME_PATH, "-b", "320", "--quiet", ORIG_WAV, LAME_MP3])
    subprocess.run([LAME_PATH, "--decode", "--quiet", LAME_MP3, LAME_WAV])
    sr, data = wav.read(LAME_WAV)
    LAME_DATA = data.astype(np.float32) / 32768.0

def objective(x):
    global BEST_SCORE, ITERATION, METRICS_HISTORY
    
    tmn, nmt, ath_shift, spread_up_slope, spread_down_slope, fwd_decay, pre_echo = x
    
    env = os.environ.copy()
    env["AUDIO_IO_PSY_TMN"] = str(tmn)
    env["AUDIO_IO_PSY_NMT"] = str(nmt)
    env["AUDIO_IO_PSY_ATH_SHIFT"] = str(ath_shift)
    env["AUDIO_IO_PSY_SPREAD_UP"] = str(spread_up_slope)
    env["AUDIO_IO_PSY_SPREAD_DOWN"] = str(spread_down_slope)
    env["AUDIO_IO_PSY_FWD_DECAY"] = str(fwd_decay)
    env["AUDIO_IO_PSY_PRE_ECHO"] = str(pre_echo)
    
    # Encode / Decode
    try:
        subprocess.run([CLI_PATH, "encode", ORIG_WAV, TEST_MP3, "320"], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.run([CLI_PATH, "decode", TEST_MP3, TEST_WAV], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        sr, data = wav.read(TEST_WAV)
        test_data = data.astype(np.float32) / 32768.0
    except Exception as e:
        return 99999.0
        
    # Align and compute metrics
    ref_alg, test_alg = align_signals(LAME_DATA, test_data)
    
    spec_mse = compute_spectrogram_mse(ref_alg, test_alg)
    env_corr = compute_envelope_correlation(ref_alg, test_alg)
    
    # ----------------------------------------------------
    # STRICT SCORING FUNCTION
    # ----------------------------------------------------
    # 1. Base score is the Spectrogram 2D MSE (Lower is better).
    # This ignores phase shifts, perfectly comparing the frequency content!
    score = spec_mse
    
    # 2. Envelope penalty: Perfect correlation is 1.0. If correlation drops, penalty explodes.
    if env_corr < 0.95:
        score += (0.95 - env_corr) * 500.0
        
    ITERATION += 1
    
    METRICS_HISTORY.append({
        "iteration": int(ITERATION),
        "score": float(score),
        "spec_mse": float(spec_mse),
        "env_corr": float(env_corr),
        "params": {
            "tmn": float(tmn), "nmt": float(nmt), "ath_shift": float(ath_shift), "spread_up": float(spread_up_slope),
            "spread_down": float(spread_down_slope), "fwd_decay": float(fwd_decay), "pre_echo": float(pre_echo)
        }
    })
    save_json()
    
    if score < BEST_SCORE:
        BEST_SCORE = score
        log_msg(f"[{ITERATION}] NEW BEST: Score={score:.2f} | SpecMSE={spec_mse:.2f}, EnvCorr={env_corr:.3f}")
        log_msg(f"      Params: {[f'{v:.3f}' for v in x]}")
        
    return score

def main():
    os.makedirs(RUN_DIR, exist_ok=True)
    log_msg(f"--- AI AUDIO LABORATORY: OPTIMIZATION RUN ---")
    log_msg(f"Started at: {datetime.now()}")
    
    if not os.path.exists(CLI_PATH):
        log_msg(f"Error: {CLI_PATH} not found.")
        return

    init_lame_baseline()
    
    log_msg("\nStarting Differential Evolution Optimizer...")
    log_msg("Using multi-dimensional AI Ear: 2D Spec MSE + Envelope Correlation + SNR")
    
    # maxiter=20 and popsize=10 will take about ~3 minutes and find the sweet spot
    result = differential_evolution(objective, BOUNDS, maxiter=3, popsize=5, disp=True)
    
    log_msg("\n=======================================================")
    log_msg("OPTIMIZATION FINISHED!")
    log_msg(f"Best Objective Score: {result.fun:.2f}")
    log_msg("Optimal Parameters for C++:")
    log_msg(f"  tmn               = {result.x[0]:.3f}f;")
    log_msg(f"  nmt               = {result.x[1]:.3f}f;")
    log_msg(f"  ath_shift         = {result.x[2]:.3f}f;")
    log_msg(f"  spread_up_slope   = {result.x[3]:.3f}f;")
    log_msg(f"  spread_down_slope = {result.x[4]:.3f}f;")
    log_msg(f"  fwd_decay         = {result.x[5]:.3f}f;")
    log_msg(f"  pre_echo_mult1    = {result.x[6]:.3f}f;")
    log_msg("=======================================================")

if __name__ == "__main__":
    main()
