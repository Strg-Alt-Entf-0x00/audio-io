import numpy as np
import scipy.signal as signal
import scipy.io.wavfile as wav

def generate_torture_test(filename, sample_rate=44100, duration_secs=5):
    """
    Generates a highly synthetic torture test designed to break audio codecs.
    Contains:
    - Logarithmic Sine Sweep (20 Hz to 20 kHz)
    - Burst Envelope White Noise (simulates Hi-Hats / Cymbals)
    - Hard Impulses (tests transient smearing / pre-echo)
    """
    print(f"Generating torture test signal ({duration_secs}s)...")
    t = np.linspace(0, duration_secs, int(sample_rate * duration_secs), endpoint=False)
    
    # 1. Synchronization Click (for precise alignment)
    sync_click = np.zeros(len(t))
    sync_click[100:110] = [1.0, -1.0, 0.8, -0.8, 0.5, -0.5, 0.2, -0.2, 0.1, -0.1]
    
    # 2. Logarithmic Sweep
    sweep = signal.chirp(t, f0=20, f1=20000, t1=duration_secs, method='log') * 0.4
    
    # 2. Burst Noise
    noise = np.random.normal(0, 0.1, len(t))
    burst_envelope = signal.square(2 * np.pi * 2 * t)
    burst_envelope = np.where(burst_envelope > 0, 1.0, 0.0)
    noise *= burst_envelope
    
    # 3. Transients / Impulses
    impulses = np.zeros(len(t))
    impulse_indices = np.arange(0, len(t), int(sample_rate * 0.4))
    for idx in impulse_indices:
        if idx < len(t):
            impulses[idx:idx+10] = [0.8, -0.6, 0.4, -0.2, 0.1, -0.05, 0.02, -0.01, 0, 0]
            
    mixed = sync_click + sweep + noise + impulses
    
    # Normalize to -1.0 dBFS (0.89)
    mixed = mixed / np.max(np.abs(mixed)) * 0.89
    
    # Stereo format
    stereo = np.column_stack((mixed, mixed))
    
    # Write to file
    wav.write(filename, sample_rate, (stereo * 32767).astype(np.int16))
    return stereo
