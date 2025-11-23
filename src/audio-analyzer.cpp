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
#include <QDebug>
#include <QFileInfo>
#include <cmath>

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
	int ret = avformat_open_input(&formatContext, filePath.toUtf8().constData(),
				      nullptr, nullptr);
	if (ret < 0) {
		qWarning() << "Could not open file:" << filePath;
		return 0.0;
	}

	ret = avformat_find_stream_info(formatContext, nullptr);
	if (ret < 0) {
		avformat_close_input(&formatContext);
		return 0.0;
	}

	double duration = 0.0;
	if (formatContext->duration != AV_NOPTS_VALUE) {
		duration = (double)formatContext->duration / AV_TIME_BASE;
	}

	avformat_close_input(&formatContext);
	return duration;
}

QVector<AudioSample> AudioAnalyzer::extractAudioSamples(const QString &filePath)
{
	QVector<AudioSample> samples;

	AVFormatContext *formatContext = nullptr;
	int ret = avformat_open_input(&formatContext, filePath.toUtf8().constData(),
				      nullptr, nullptr);
	if (ret < 0) {
		qWarning() << "Could not open file:" << filePath;
		return samples;
	}

	ret = avformat_find_stream_info(formatContext, nullptr);
	if (ret < 0) {
		avformat_close_input(&formatContext);
		return samples;
	}

	// Find audio stream
	int audioStreamIndex = -1;
	for (unsigned int i = 0; i < formatContext->nb_streams; i++) {
		if (formatContext->streams[i]->codecpar->codec_type ==
		    AVMEDIA_TYPE_AUDIO) {
			audioStreamIndex = i;
			break;
		}
	}

	if (audioStreamIndex == -1) {
		qWarning() << "No audio stream found";
		avformat_close_input(&formatContext);
		return samples;
	}

	AVCodecParameters *codecParams =
		formatContext->streams[audioStreamIndex]->codecpar;
	const AVCodec *codec = avcodec_find_decoder(codecParams->codec_id);
	if (!codec) {
		qWarning() << "Codec not found";
		avformat_close_input(&formatContext);
		return samples;
	}

	AVCodecContext *codecContext = avcodec_alloc_context3(codec);
	if (!codecContext) {
		avformat_close_input(&formatContext);
		return samples;
	}

	ret = avcodec_parameters_to_context(codecContext, codecParams);
	if (ret < 0) {
		avcodec_free_context(&codecContext);
		avformat_close_input(&formatContext);
		return samples;
	}

	ret = avcodec_open2(codecContext, codec, nullptr);
	if (ret < 0) {
		avcodec_free_context(&codecContext);
		avformat_close_input(&formatContext);
		return samples;
	}

	// Get sample rate for timestamp calculation
	double sampleRate = codecContext->sample_rate;
	double timeBase =
		av_q2d(formatContext->streams[audioStreamIndex]->time_base);

	AVPacket *packet = av_packet_alloc();
	AVFrame *frame = av_frame_alloc();

	// Read packets and decode
	while (av_read_frame(formatContext, packet) >= 0) {
		if (packet->stream_index == audioStreamIndex) {
			ret = avcodec_send_packet(codecContext, packet);
			if (ret < 0) {
				av_packet_unref(packet);
				continue;
			}

			while (ret >= 0) {
				ret = avcodec_receive_frame(codecContext, frame);
				if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
					break;
				} else if (ret < 0) {
					break;
				}

				// Calculate RMS amplitude for this frame
				double rms = 0.0;
				int sampleCount = frame->nb_samples * frame->channels;

				if (frame->format == AV_SAMPLE_FMT_FLTP) {
					// Planar float format - each channel is separate
					for (int ch = 0; ch < frame->channels; ch++) {
						float *channelData = (float *)frame->data[ch];
						for (int i = 0; i < frame->nb_samples; i++) {
							rms += channelData[i] * channelData[i];
						}
					}
					rms = sqrt(rms / sampleCount);
				} else if (frame->format == AV_SAMPLE_FMT_FLT) {
					// Interleaved float format
					float *data = (float *)frame->data[0];
					for (int i = 0; i < sampleCount; i++) {
						rms += data[i] * data[i];
					}
					rms = sqrt(rms / sampleCount);
				} else if (frame->format == AV_SAMPLE_FMT_S16P) {
					// Planar 16-bit integer format
					for (int ch = 0; ch < frame->channels; ch++) {
						int16_t *channelData = (int16_t *)frame->data[ch];
						for (int i = 0; i < frame->nb_samples; i++) {
							double normalized =
								(double)channelData[i] / 32768.0;
							rms += normalized * normalized;
						}
					}
					rms = sqrt(rms / sampleCount);
				} else if (frame->format == AV_SAMPLE_FMT_S16) {
					// Interleaved 16-bit integer format
					int16_t *data = (int16_t *)frame->data[0];
					for (int i = 0; i < sampleCount; i++) {
						double normalized = (double)data[i] / 32768.0;
						rms += normalized * normalized;
					}
					rms = sqrt(rms / sampleCount);
				}

				// Calculate timestamp
				double timestamp = frame->pts * timeBase;
				if (timestamp < 0) {
					timestamp = 0; // Handle invalid timestamps
				}

				AudioSample sample;
				sample.timestamp = timestamp;
				sample.amplitude = rms;
				samples.append(sample);
			}
		}
		av_packet_unref(packet);
	}

	av_frame_free(&frame);
	av_packet_free(&packet);
	avcodec_free_context(&codecContext);
	avformat_close_input(&formatContext);

	return samples;
}

AudioSpike AudioAnalyzer::findLargestSpike(const QVector<AudioSample> &samples)
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
	QFileInfo fileInfo(filePath);
	if (!fileInfo.exists()) {
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

QVector<AudioSample> AudioAnalyzer::getAudioSamples(const QString &filePath,
						     double startTime,
						     double endTime)
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
