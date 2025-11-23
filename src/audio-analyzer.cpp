/*
OBS Audio Sync Plugin
Copyright (C) 2025 Ryan Wooden

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "audio-analyzer.h"
#include "libavcodec/codec.h"
#include "libavutil/rational.h"
#include "libavcodec/packet.h"
#include "libavutil/frame.h"
#include "libavutil/error.h"
#include <QDebug>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <qlogging.h>
#include <cstdint>

// FFmpeg includes
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/samplefmt.h>
#include <libavutil/channel_layout.h>
}

AudioAnalyzer::AudioAnalyzer()
{
	initializeFFmpeg();
}

AudioAnalyzer::~AudioAnalyzer()
{
	cleanupFFmpeg();
}

bool AudioAnalyzer::initializeFFmpeg()
{
	// FFmpeg initialization is typically done automatically
	// but we can register formats/codecs if needed
	return true;
}

void AudioAnalyzer::cleanupFFmpeg()
{
	// Cleanup if needed
}

double AudioAnalyzer::getFileDuration(const QString &filePath)
{
	AVFormatContext *formatContext = nullptr;
	int ret = avformatOpenInput(&formatContext, filePath.toUtf8().constData(), nullptr, nullptr);
	if (ret < 0) {
		qWarning() << "Could not open file:" << filePath;
		return 0.0;
	}

	ret = avformatFindStreamInfo(formatContext, nullptr);
	if (ret < 0) {
		avformatCloseInput(&formatContext);
		return 0.0;
	}

	double duration = 0.0;
	if (formatContext->duration != AV_NOPTS_VALUE) {
		duration = (double)formatContext->duration / AV_TIME_BASE;
	}

	avformatCloseInput(&formatContext);
	return duration;
}

namespace {
// Helper function to find audio stream index
int findAudioStreamIndex(AVFormatContext *formatContext)
{
	for (unsigned int i = 0; i < formatContext->nbStreams; i++) {
		if (formatContext->streams[i]->codecpar->codecType == AVMEDIA_TYPE_AUDIO) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

// Helper function to setup codec context
AVCodecContext *setupCodecContext(AVFormatContext *formatContext, int audioStreamIndex)
{
	AVCodecParameters const *codecParams = formatContext->streams[audioStreamIndex]->codecpar;
	const AVCodec *codec = avcodecFindDecoder(codecParams->codecId);
	if (codec == nullptr) {
		qWarning() << "Codec not found";
		return nullptr;
	}

	AVCodecContext *codecContext = avcodecAllocContext3(codec);
	if (codecContext == nullptr) {
		return nullptr;
	}

	int ret = avcodecParametersToContext(codecContext, codecParams);
	if (ret < 0) {
		avcodecFreeContext(&codecContext);
		return nullptr;
	}

	ret = avcodecOpen2(codecContext, codec, nullptr);
	if (ret < 0) {
		avcodecFreeContext(&codecContext);
		return nullptr;
	}

	return codecContext;
}

// Helper function to calculate RMS amplitude for a frame
double calculateFrameRMS(AVFrame const *frame)
{
	int const CHANNELS = frame->chLayout.nbChannels;
	int const SAMPLE_COUNT = frame->nbSamples * CHANNELS;
	double rms = 0.0;

	if (frame->format == AV_SAMPLE_FMT_FLTP) {
		// Planar float format - each channel is separate
		for (int ch = 0; ch < CHANNELS; ch++) {
			auto const *channelData = reinterpret_cast<float const *>(frame->data[ch]);
			for (int i = 0; i < frame->nbSamples; i++) {
				rms += channelData[i] * channelData[i];
			}
		}
		rms = sqrt(rms / SAMPLE_COUNT);
	} else if (frame->format == AV_SAMPLE_FMT_FLT) {
		// Interleaved float format
		auto const *data = reinterpret_cast<float const *>(frame->data[0]);
		for (int i = 0; i < SAMPLE_COUNT; i++) {
			rms += data[i] * data[i];
		}
		rms = sqrt(rms / SAMPLE_COUNT);
	} else if (frame->format == AV_SAMPLE_FMT_S16P) {
		// Planar 16-bit integer format
		for (int ch = 0; ch < CHANNELS; ch++) {
			auto const *channelData = reinterpret_cast<int16_t const *>(frame->data[ch]);
			for (int i = 0; i < frame->nbSamples; i++) {
				double const NORMALIZED = static_cast<double>(channelData[i]) / 32768.0;
				rms += NORMALIZED * NORMALIZED;
			}
		}
		rms = sqrt(rms / SAMPLE_COUNT);
	} else if (frame->format == AV_SAMPLE_FMT_S16) {
		// Interleaved 16-bit integer format
		auto const *data = reinterpret_cast<int16_t const *>(frame->data[0]);
		for (int i = 0; i < SAMPLE_COUNT; i++) {
			double const NORMALIZED = static_cast<double>(data[i]) / 32768.0;
			rms += NORMALIZED * NORMALIZED;
		}
		rms = sqrt(rms / SAMPLE_COUNT);
	}

	return rms;
}
} // namespace

static QVector<AudioSample> AudioAnalyzer::extractAudioSamples(const QString &filePath)
{
	QVector<AudioSample> samples;

	AVFormatContext *formatContext = nullptr;
	int ret = avformatOpenInput(&formatContext, filePath.toUtf8().constData(), nullptr, nullptr);
	if (ret < 0) {
		qWarning() << "Could not open file:" << filePath;
		return samples;
	}

	ret = avformatFindStreamInfo(formatContext, nullptr);
	if (ret < 0) {
		avformatCloseInput(&formatContext);
		return samples;
	}

	int const AUDIO_STREAM_INDEX = findAudioStreamIndex(formatContext);
	if (AUDIO_STREAM_INDEX == -1) {
		qWarning() << "No audio stream found";
		avformatCloseInput(&formatContext);
		return samples;
	}

	AVCodecContext *codecContext = setupCodecContext(formatContext, AUDIO_STREAM_INDEX);
	if (codecContext == nullptr) {
		avformatCloseInput(&formatContext);
		return samples;
	}

	// Get time base for timestamp calculation
	double const TIME_BASE = avQ2d(formatContext->streams[AUDIO_STREAM_INDEX]->timeBase);

	AVPacket *packet = avPacketAlloc();
	AVFrame *frame = avFrameAlloc();

	// Read packets and decode
	while (avReadFrame(formatContext, packet) >= 0) {
		if (packet->streamIndex == AUDIO_STREAM_INDEX) {
			ret = avcodecSendPacket(codecContext, packet);
			if (ret < 0) {
				avPacketUnref(packet);
				continue;
			}

			while (ret >= 0) {
				ret = avcodecReceiveFrame(codecContext, frame);
				if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
					break;
				}
				if (ret < 0) {
					break;
				}

				double const RMS = calculateFrameRMS(frame);
				double const TIMESTAMP = static_cast<double>(frame->pts) * TIME_BASE;

				AudioSample sample{};
				sample.timestamp = TIMESTAMP;
				sample.amplitude = RMS;
				samples.append(sample);
			}
		}
		avPacketUnref(packet);
	}

	avFrameFree(&frame);
	avPacketFree(&packet);
	avcodecFreeContext(&codecContext);
	avformatCloseInput(&formatContext);

	return samples;
}

static AudioSpike AudioAnalyzer::findLargestSpike(const QVector<AudioSample> &samples)
{
	AudioSpike spike = {0.0, 0.0, 0.0, 0.0};

	if (samples.isEmpty()) {
		return spike;
	}

	// Find sample with maximum amplitude
	double maxAmplitude = 0.0;
	int maxIndex = 0;

	for (int i = 0; i < samples.size(); i++) {
		if (samples[i].amplitude > maxAmplitude) {
			maxAmplitude = samples[i].amplitude;
			maxIndex = i;
		}
	}

	spike.timestamp = samples[maxIndex].timestamp;
	spike.amplitude = maxAmplitude;

	// Calculate 4-second window (2s before, 2s after)
	// Clamp to file boundaries
	double fileDuration = samples.isEmpty() ? 0.0 : samples.last().timestamp;
	spike.windowStart = qMax(0.0, spike.timestamp - 2.0);
	spike.windowEnd = qMin(fileDuration, spike.timestamp + 2.0);

	return spike;
}

bool AudioAnalyzer::analyzeFile(const QString &filePath, AudioSpike &spike)
{
	QFileInfo const FILE_INFO(filePath);
	if (!FILE_INFO.exists()) {
		qWarning() << "File does not exist:" << filePath;
		return false;
	}

	QVector<AudioSample> samples = extractAudioSamples(filePath);
	if (samples.isEmpty()) {
		qWarning() << "No audio samples extracted";
		return false;
	}

	spike = findLargestSpike(samples);
	return true;
}

static QVector<AudioSample> AudioAnalyzer::getAudioSamples(const QString &filePath, double startTime, double endTime)
{
	QVector<AudioSample> allSamples = extractAudioSamples(filePath);
	QVector<AudioSample> windowSamples;

	for (const AudioSample &sample : allSamples) {
		if (sample.timestamp >= startTime && sample.timestamp <= endTime) {
			windowSamples.append(sample);
		}
	}

	return windowSamples;
}
