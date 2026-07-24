import numpy as np
import scipy.signal as signal

def align_signals(ref, test):
    """
    Aligns the test signal to the reference signal using cross-correlation.
    Handles variable delays (e.g. LAME padding vs audio-io padding).
    Uses only the first 2000 samples which contain the sharp sync click!
    """
    limit = min(2000, len(ref), len(test))
    correlation = signal.correlate(test[:limit, 0], ref[:limit, 0], mode='full')
    delay = np.argmax(correlation) - limit + 1
    
    if delay > 0:
        test_aligned = test[delay:]
        ref_aligned = ref
    elif delay < 0:
        test_aligned = test
        ref_aligned = ref[-delay:]
    else:
        test_aligned = test
        ref_aligned = ref
        
    return ref_aligned, test_aligned

def compute_spectrogram_mse(ref, test, nfft=2048):
    """
    Computes the Mean Squared Error of the 2D Log-Spectrograms for both channels.
    This effectively acts as an "AI Ear", comparing the time-frequency heatmap.
    Penalizes missing frequencies, temporal artifacts, and stereo image destruction.
    """
    limit = min(len(ref), len(test))
    ref = ref[:limit]
    test = test[:limit]
    
    mse_total = 0.0
    for ch in range(2):
        _, _, Zxx_ref = signal.stft(ref[:, ch], nperseg=nfft)
        _, _, Zxx_test = signal.stft(test[:, ch], nperseg=nfft)
        
        # Add a small epsilon to avoid log(0)
        S_ref = 20 * np.log10(np.abs(Zxx_ref) + 1e-10)
        S_test = 20 * np.log10(np.abs(Zxx_test) + 1e-10)
        
        # 2D MSE per channel
        mse_total += np.mean((S_ref - S_test)**2)
        
    return mse_total / 2.0

def compute_envelope_correlation(ref, test):
    """
    Extracts the broadband amplitude envelope and computes the Pearson correlation for both channels.
    Strictly penalizes if the noise floor rhythm is corrupted or the stereo image collapses.
    """
    limit = min(len(ref), len(test))
    ref = ref[:limit]
    test = test[:limit]
    
    window_size = 441  # 10ms at 44.1kHz
    window = np.ones(window_size) / window_size
    
    corr_total = 0.0
    for ch in range(2):
        conv_ref = signal.convolve(ref[:, ch]**2, window, mode='same')
        conv_test = signal.convolve(test[:, ch]**2, window, mode='same')
        
        env_ref = np.sqrt(np.clip(conv_ref, 0, None))
        env_test = np.sqrt(np.clip(conv_test, 0, None))
        
        correlation = np.corrcoef(env_ref, env_test)[0, 1]
        if np.isnan(correlation):
            correlation = 0.0
        corr_total += correlation
        
    return corr_total / 2.0

def compute_snr(ref, test):
    """
    Computes the Signal-to-Noise Ratio in the time domain.
    Penalizes broadband quantization noise and heavy clipping.
    """
    limit = min(len(ref), len(test))
    ref = ref[:limit]
    test = test[:limit]
    
    signal_power = np.sum(ref**2)
    noise_power = np.sum((ref - test)**2)
    
    if noise_power < 1e-10:
        return 100.0
    return 10 * np.log10(signal_power / noise_power)

def detect_clipping(test):
    """
    Returns True if the signal mathematically clips (exceeds [-1.0, 1.0]).
    """
    return np.max(np.abs(test)) > 0.999
