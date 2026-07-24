// audio_reader.cpp - Main audio reader implementation
// Part of audio-io-1.0.0
// Windows-native, C++20

#include "audio_io/audio_reader.h"
#include "resampler/sinc_resampler.h"
#include "wave/wave_decoder.h"
#include "mp3/mp3_codec.h"
#include "metadata/id3v2_reader.h"
#include "metadata/riff_metadata_reader.h"
#include <algorithm>
#include <cwctype>  // std::towlower for correct wchar_t handling

namespace audio_io {

// Detect file format from extension.
// Correctly handles mixed-case extensions (e.g. .WAV, .Mp3) using std::towlower.
FileFormat AudioReader::detectFileFormat(const wchar_t* filePathUtf16) {
    std::wstring path(filePathUtf16);

    size_t dotPos = path.find_last_of(L'.');
    if (dotPos == std::wstring::npos) {
        return FileFormat::Unknown;
    }

    std::wstring ext = path.substr(dotPos + 1);

    // Use std::towlower for correct wide-char lowercase conversion (MSVC safe)
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });

    if (ext == L"wav" || ext == L"wave") return FileFormat::Wave;
    if (ext == L"mp3")                   return FileFormat::Mp3;

    return FileFormat::Unknown;
}

// Apply ReadOptions to decoded AudioData.
// Returns AudioError with an empty message on success.
AudioError AudioReader::applyReadOptions(AudioData& audioData, const ReadOptions& opts) {
    const int native_channels   = audioData.format.channelCount;
    const int native_rate       = audioData.format.sampleRateHz;
    const int target_channels   = (opts.targetChannelCount > 0) ? opts.targetChannelCount : native_channels;
    const int target_rate       = (opts.targetSampleRateHz > 0) ? opts.targetSampleRateHz : native_rate;

    // ---- Step 1: Channel conversion (before resampling = cheaper) ----
    if (target_channels != native_channels) {
        if (native_channels == 2 && target_channels == 1) {
            // Stereo -> Mono
            const size_t frame_count = audioData.samples.size() / 2;
            audioData.samples = resampler::stereo_to_mono(audioData.samples.data(), frame_count);
            audioData.format.channelCount     = 1;
            audioData.format.totalSampleFrames = static_cast<int64_t>(frame_count);
        } else if (native_channels == 1 && target_channels == 2) {
            // Mono -> Stereo
            const size_t frame_count = audioData.samples.size();
            audioData.samples = resampler::mono_to_stereo(audioData.samples.data(), frame_count);
            audioData.format.channelCount     = 2;
            audioData.format.totalSampleFrames = static_cast<int64_t>(frame_count);
        } else {
            return AudioError{
                "Unsupported channel conversion: " +
                std::to_string(native_channels) + " -> " +
                std::to_string(target_channels) +
                " (supported: stereo->mono, mono->stereo)",
                0
            };
        }
    }

    // ---- Step 2: Resampling ----
    if (target_rate != audioData.format.sampleRateHz) {
        const int channels    = audioData.format.channelCount;
        const size_t frames_in = static_cast<size_t>(audioData.format.totalSampleFrames);

        if (channels == 1) {
            // Mono: resample directly
            audioData.samples = resampler::resample_mono(
                audioData.samples.data(), frames_in, audioData.format.sampleRateHz, target_rate);
        } else {
            // Multi-channel: resample each channel independently, re-interleave
            std::vector<float> resampled;
            // We process channels one at a time to avoid allocating a huge deinterleaved buffer
            // For stereo: deinterleave -> resample L -> resample R -> interleave
            std::vector<float> deinterleaved_ch(frames_in);
            std::vector<std::vector<float>> resampled_channels(channels);

            for (int ch = 0; ch < channels; ++ch) {
                for (size_t f = 0; f < frames_in; ++f) {
                    deinterleaved_ch[f] = audioData.samples[f * channels + ch];
                }
                resampled_channels[ch] = resampler::resample_mono(
                    deinterleaved_ch.data(), frames_in, audioData.format.sampleRateHz, target_rate);
            }

            const size_t frames_out = resampled_channels[0].size();
            resampled.resize(frames_out * channels);
            for (int ch = 0; ch < channels; ++ch) {
                for (size_t f = 0; f < frames_out; ++f) {
                    resampled[f * channels + ch] = resampled_channels[ch][f];
                }
            }
            audioData.samples = std::move(resampled);
        }

        audioData.format.sampleRateHz     = target_rate;
        audioData.format.totalSampleFrames = static_cast<int64_t>(audioData.samples.size() / channels);
    }

    return AudioError{"", 0};  // Success: empty message
}

// ============================================================================
// Public API
// ============================================================================

Expected<AudioFormat, AudioError> AudioReader::readFormat(const wchar_t* filePathUtf16) {
    FileFormat format = detectFileFormat(filePathUtf16);
    switch (format) {
        case FileFormat::Wave: return readWaveFormat(filePathUtf16);
        case FileFormat::Mp3:  return readMp3Format(filePathUtf16);
        default:
            return makeUnexpected(AudioError{
                "Unsupported audio file format (only WAV and MP3 supported)", 0});
    }
}

Expected<AudioData, AudioError> AudioReader::readFile(
    const wchar_t* filePathUtf16,
    const ReadOptions& opts)
{
    FileFormat fmt = detectFileFormat(filePathUtf16);

    Expected<AudioData, AudioError> result = makeUnexpected(
        AudioError{"Unsupported audio file format (only WAV and MP3 supported)", 0});

    switch (fmt) {
        case FileFormat::Wave: result = readWaveFile(filePathUtf16); break;
        case FileFormat::Mp3:  result = readMp3File(filePathUtf16);  break;
        default: return result;
    }

    if (!result.has_value()) return result;

    // Apply channel conversion and resampling if requested
    const bool needs_channel = (opts.targetChannelCount > 0 &&
                                 opts.targetChannelCount != result.value().format.channelCount);
    const bool needs_resample = (opts.targetSampleRateHz > 0 &&
                                  opts.targetSampleRateHz != result.value().format.sampleRateHz);

    if (needs_channel || needs_resample) {
        AudioError err = applyReadOptions(result.value(), opts);
        if (!err.message.empty()) {
            return makeUnexpected(err);
        }
    }

    return result;
}

// ============================================================================
// Format-specific readers (delegates)
// ============================================================================

Expected<AudioData, AudioError> AudioReader::readWaveFile(const wchar_t* filePathUtf16) {
    wave::WaveDecoder decoder;
    return decoder.decode(filePathUtf16);
}

Expected<AudioFormat, AudioError> AudioReader::readWaveFormat(const wchar_t* filePathUtf16) {
    wave::WaveDecoder decoder;
    return decoder.readFormat(filePathUtf16);
}

Expected<AudioData, AudioError> AudioReader::readMp3File(const wchar_t* filePathUtf16) {
    mp3::Mp3Codec decoder;
    return decoder.decode(filePathUtf16);
}

Expected<AudioFormat, AudioError> AudioReader::readMp3Format(const wchar_t* filePathUtf16) {
    mp3::Mp3Codec decoder;
    return decoder.readFormat(filePathUtf16);
}

// ============================================================================
// Metadata
// ============================================================================

Expected<AudioMetadata, AudioError> AudioReader::readMetadata(const wchar_t* filePathUtf16) {
    FileFormat format = detectFileFormat(filePathUtf16);

    AudioMetadata metadata{};

    switch (format) {
        case FileFormat::Mp3: {
            auto metadataOpt = metadata::Id3v2Reader::readFromFile(filePathUtf16);
            if (metadataOpt) metadata = *metadataOpt;
            break;
        }
        case FileFormat::Wave: {
            auto metadataOpt = metadata::RiffMetadataReader::readFromFile(filePathUtf16);
            if (metadataOpt) metadata = *metadataOpt;
            break;
        }
        default:
            return makeUnexpected(AudioError{
                "Unsupported file format for metadata reading", 0});
    }

    // Populate duration from format if not in tags
    if (metadata.durationMs == 0) {
        auto formatResult = readFormat(filePathUtf16);
        if (formatResult.has_value()) {
            metadata.durationMs = static_cast<int>(
                formatResult.value().durationSeconds() * 1000.0);
        }
    }

    return metadata;
}

} // namespace audio_io
