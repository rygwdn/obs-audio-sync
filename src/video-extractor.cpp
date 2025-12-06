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

#include "video-extractor.h"
#include <QDebug>
#include <QImage>
#include <qlogging.h>
#include <cstdint>
#include <qpixmap.h>

// FFmpeg includes
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavutil/pixfmt.h>
#include <libavutil/error.h>
#include <libavutil/rational.h>
#include <libavcodec/codec_par.h>
#include <libavcodec/codec.h>
#include <libavcodec/packet.h>
#include <libswscale/swscale.h>
}

VideoExtractor::VideoExtractor() : m_fps(30.0) {}

VideoExtractor::~VideoExtractor()
{
	close();
}

bool VideoExtractor::openFile(const QString &filePath)
{
	close(); // Close any previously opened file

	m_filePath = filePath;

	AVFormatContext *formatContext = nullptr;
	int ret = avformat_open_input(&formatContext, filePath.toUtf8().constData(), nullptr, nullptr);
	if (ret < 0) {
		qWarning() << "Could not open video file:" << filePath;
		return false;
	}

	ret = avformat_find_stream_info(formatContext, nullptr);
	if (ret < 0) {
		avformat_close_input(&formatContext);
		return false;
	}

	// Find video stream
	int videoStreamIndex = -1;
	for (unsigned int i = 0; i < formatContext->nb_streams; i++) {
		if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			videoStreamIndex = static_cast<int>(i);
			break;
		}
	}

	if (videoStreamIndex == -1) {
		qWarning() << "No video stream found";
		avformat_close_input(&formatContext);
		return false;
	}

	// Get FPS and duration
	AVStream const *videoStream = formatContext->streams[videoStreamIndex];
	if (videoStream->r_frame_rate.num > 0 && videoStream->r_frame_rate.den > 0) {
		m_fps = av_q2d(videoStream->r_frame_rate);
	} else {
		m_fps = 30.0; // Default
	}

	if (formatContext->duration != AV_NOPTS_VALUE) {
		m_duration = (double)formatContext->duration / AV_TIME_BASE;
	}

	avformat_close_input(&formatContext);
	m_fileOpen = true;

	return true;
}

VideoExtractor::FormatContextData VideoExtractor::setupFormatContext(const QString &filePath)
{
	FormatContextData data{};

	AVFormatContext *formatContext = nullptr;
	int ret = avformat_open_input(&formatContext, filePath.toUtf8().constData(), nullptr, nullptr);
	if (ret < 0) {
		return data;
	}

	ret = avformat_find_stream_info(formatContext, nullptr);
	if (ret < 0) {
		avformat_close_input(&formatContext);
		return data;
	}

	// Find video stream
	int videoStreamIndex = -1;
	for (unsigned int i = 0; i < formatContext->nb_streams; i++) {
		if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			videoStreamIndex = static_cast<int>(i);
			break;
		}
	}

	if (videoStreamIndex == -1) {
		avformat_close_input(&formatContext);
		return data;
	}

	data.formatContext = formatContext;
	data.videoStreamIndex = videoStreamIndex;
	data.videoStream = formatContext->streams[videoStreamIndex];
	return data;
}

void VideoExtractor::cleanupFormatContext(FormatContextData &data)
{
	if (data.formatContext != nullptr) {
		avformat_close_input(&data.formatContext);
	}
	data = FormatContextData{};
}

VideoExtractor::CodecContextData VideoExtractor::setupCodecContext(const FormatContextData &formatData)
{
	CodecContextData data{};

	if (formatData.formatContext == nullptr) {
		return data;
	}

	AVCodecParameters const *codecParams = formatData.formatContext->streams[formatData.videoStreamIndex]->codecpar;
	const AVCodec *codec = avcodec_find_decoder(codecParams->codec_id);
	if (codec == nullptr) {
		return data;
	}

	AVCodecContext *codecContext = avcodec_alloc_context3(codec);
	if (codecContext == nullptr) {
		return data;
	}

	int ret = avcodec_parameters_to_context(codecContext, codecParams);
	if (ret < 0) {
		avcodec_free_context(&codecContext);
		return data;
	}

	ret = avcodec_open2(codecContext, codec, nullptr);
	if (ret < 0) {
		avcodec_free_context(&codecContext);
		return data;
	}

	// Allocate frame for decoding
	AVFrame *avFrame = av_frame_alloc();
	AVFrame *rgbFrame = av_frame_alloc();
	AVPacket *packet = av_packet_alloc();

	// Allocate buffer for RGB frame
	int const NUM_BYTES = av_image_get_buffer_size(AV_PIX_FMT_RGB24, codecContext->width, codecContext->height, 1);
	auto *buffer = (uint8_t *)av_malloc(NUM_BYTES * sizeof(uint8_t));
	av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, buffer, AV_PIX_FMT_RGB24, codecContext->width,
			     codecContext->height, 1);

	// Create sws context for conversion
	SwsContext *swsContext = sws_getContext(codecContext->width, codecContext->height, codecContext->pix_fmt,
						codecContext->width, codecContext->height, AV_PIX_FMT_RGB24,
						SWS_BILINEAR, nullptr, nullptr, nullptr);

	data.codecContext = codecContext;
	data.swsContext = swsContext;
	data.avFrame = avFrame;
	data.rgbFrame = rgbFrame;
	data.packet = packet;
	data.buffer = buffer;
	return data;
}

void VideoExtractor::cleanupCodecContext(CodecContextData &data)
{
	if (data.swsContext != nullptr) {
		sws_freeContext(data.swsContext);
	}
	if (data.buffer != nullptr) {
		av_free(data.buffer);
	}
	if (data.rgbFrame != nullptr) {
		av_frame_free(&data.rgbFrame);
	}
	if (data.avFrame != nullptr) {
		av_frame_free(&data.avFrame);
	}
	if (data.packet != nullptr) {
		av_packet_free(&data.packet);
	}
	if (data.codecContext != nullptr) {
		avcodec_free_context(&data.codecContext);
	}
	data = CodecContextData{};
}

bool VideoExtractor::processDecodedFrame(const FormatContextData &formatData, const CodecContextData &codecData,
					 AVFrame *decodedFrame, double timestamp, double &bestTimeDiff,
					 AVFrame *&currentBestFrame)
{
	double const FRAME_TIME = decodedFrame->pts * av_q2d(formatData.videoStream->time_base);
	double timeDiff = qAbs(FRAME_TIME - timestamp);

	if (timeDiff < bestTimeDiff) {
		bestTimeDiff = timeDiff;
		// Convert to RGB
		sws_scale(codecData.swsContext, (const uint8_t *const *)decodedFrame->data, decodedFrame->linesize, 0,
			  codecData.codecContext->height, codecData.rgbFrame->data, codecData.rgbFrame->linesize);

		// Copy frame data
		if (currentBestFrame != nullptr) {
			av_frame_free(&currentBestFrame);
		}
		currentBestFrame = av_frame_clone(codecData.rgbFrame);
	}

	// If we've passed the timestamp, stop processing
	return FRAME_TIME <= timestamp + 0.1;
}

bool VideoExtractor::decodeVideoPackets(const FormatContextData &formatData, const CodecContextData &codecData,
					double timestamp, double &bestTimeDiff, AVFrame *&bestFrame)
{
	int ret = avcodec_send_packet(codecData.codecContext, codecData.packet);
	if (ret < 0) {
		return true; // Continue reading packets
	}

	while (ret >= 0) {
		ret = avcodec_receive_frame(codecData.codecContext, codecData.avFrame);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
			break;
		}
		if (ret < 0) {
			break;
		}

		bool const SHOULD_CONTINUE = processDecodedFrame(formatData, codecData, codecData.avFrame, timestamp,
								 bestTimeDiff, bestFrame);
		if (!SHOULD_CONTINUE) {
			return false; // Time exceeded, stop processing
		}
	}

	return bestTimeDiff >= 0.05; // Continue if not close enough
}

AVFrame *VideoExtractor::findBestFrame(const FormatContextData &formatData, const CodecContextData &codecData,
				       double timestamp)
{
	if (formatData.formatContext == nullptr || codecData.codecContext == nullptr) {
		return nullptr;
	}

	// Seek to timestamp
	auto seekTarget = (int64_t)(timestamp / av_q2d(formatData.videoStream->time_base));
	int const RET =
		av_seek_frame(formatData.formatContext, formatData.videoStreamIndex, seekTarget, AVSEEK_FLAG_BACKWARD);
	if (RET < 0) {
		return nullptr;
	}

	// Read and decode frames until we find the one closest to timestamp
	double bestTimeDiff = 1e10;
	AVFrame *bestFrame = nullptr;

	while (av_read_frame(formatData.formatContext, codecData.packet) >= 0) {
		if (codecData.packet->stream_index == formatData.videoStreamIndex) {
			bool const SHOULD_CONTINUE =
				decodeVideoPackets(formatData, codecData, timestamp, bestTimeDiff, bestFrame);
			if (!SHOULD_CONTINUE) {
				av_packet_unref(codecData.packet);
				break;
			}
		}
		av_packet_unref(codecData.packet);

		if (bestTimeDiff < 0.05) { // Close enough (50ms)
			break;
		}
	}

	return bestFrame;
}

VideoFrame VideoExtractor::extractFrameAt(double timestamp) const
{
	VideoFrame frame;
	frame.timestamp = timestamp;
	frame.frameNumber = -1;

	if (!m_fileOpen || m_filePath.isEmpty()) {
		return frame;
	}

	FormatContextData formatData = setupFormatContext(m_filePath);
	if (formatData.formatContext == nullptr) {
		return frame;
	}

	CodecContextData codecData = setupCodecContext(formatData);
	if (codecData.codecContext == nullptr) {
		cleanupFormatContext(formatData);
		return frame;
	}

	AVFrame *bestFrame = findBestFrame(formatData, codecData, timestamp);

	// Convert to QPixmap
	if (bestFrame != nullptr) {
		QImage image(bestFrame->data[0], codecData.codecContext->width, codecData.codecContext->height,
			     bestFrame->linesize[0], QImage::Format_RGB888);
		frame.pixmap = QPixmap::fromImage(image);
		frame.frameNumber = (int)(timestamp * m_fps);
		av_frame_free(&bestFrame);
	}

	// Cleanup
	cleanupCodecContext(codecData);
	cleanupFormatContext(formatData);

	return frame;
}

QVector<VideoFrame> VideoExtractor::extractFrames(double startTime, double endTime)
{
	QVector<VideoFrame> frames;

	if (!m_fileOpen || m_filePath.isEmpty() || m_fps <= 0.0) {
		qWarning() << "VideoExtractor::extractFrames: File not open or invalid FPS";
		return frames;
	}

	// Setup format context
	FormatContextData formatData = setupFormatContext(m_filePath);
	if (formatData.formatContext == nullptr) {
		qWarning() << "VideoExtractor::extractFrames: Failed to setup format context";
		return frames;
	}

	// Setup codec context
	CodecContextData codecData = setupCodecContext(formatData);
	if (codecData.codecContext == nullptr) {
		qWarning() << "VideoExtractor::extractFrames: Failed to setup codec context";
		cleanupFormatContext(formatData);
		return frames;
	}

	// Seek to start time
	auto seekTarget = (int64_t)(startTime / av_q2d(formatData.videoStream->time_base));
	int const SEEK_RET =
		av_seek_frame(formatData.formatContext, formatData.videoStreamIndex, seekTarget, AVSEEK_FLAG_BACKWARD);
	if (SEEK_RET < 0) {
		qWarning() << "VideoExtractor::extractFrames: Failed to seek to start time";
		cleanupCodecContext(codecData);
		cleanupFormatContext(formatData);
		return frames;
	}

	// Flush codec buffers after seek
	avcodec_flush_buffers(codecData.codecContext);

	double const FRAME_DURATION = 1.0 / m_fps;
	double const TOLERANCE = FRAME_DURATION * 0.5; // Half a frame duration tolerance
	int frameNumber = 0;

	// Calculate target frame times
	QVector<double> targetTimes;
	for (double t = startTime; t <= endTime + TOLERANCE; t += FRAME_DURATION) {
		targetTimes.append(t);
	}

	if (targetTimes.isEmpty()) {
		cleanupCodecContext(codecData);
		cleanupFormatContext(formatData);
		return frames;
	}

	// Decode frames sequentially and match them to target times
	int targetIndex = 0;
	bool eofReached = false;
	AVFrame *lastDecodedFrame = nullptr;
	double lastDecodedTime = -1.0;

	while (!eofReached && targetIndex < targetTimes.size()) {
		double const TARGET_TIME = targetTimes[targetIndex];

		// Check if we can reuse the last decoded frame
		if (lastDecodedFrame != nullptr && lastDecodedTime >= 0.0) {
			double timeDiff = qAbs(lastDecodedTime - TARGET_TIME);
			if (timeDiff < TOLERANCE) {
				// Reuse last frame
				VideoFrame frame;
				frame.timestamp = lastDecodedTime;
				frame.frameNumber = frameNumber;

				// Convert to QPixmap (copy image data to ensure it persists)
				QImage image(lastDecodedFrame->data[0], codecData.codecContext->width,
					     codecData.codecContext->height, lastDecodedFrame->linesize[0],
					     QImage::Format_RGB888);
				frame.pixmap = QPixmap::fromImage(image.copy());
				frames.append(frame);

				frameNumber++;
				targetIndex++;
				continue;
			}
		}

		// Need to decode more frames
		AVFrame *bestFrame = nullptr;
		double bestFrameTime = -1.0;
		double bestTimeDiff = 1e10;

		while (!eofReached) {
			int readRet = av_read_frame(formatData.formatContext, codecData.packet);
			if (readRet < 0) {
				eofReached = true;
				// Try to flush remaining frames from decoder
				avcodec_send_packet(codecData.codecContext, nullptr);
				break;
			}

			if (codecData.packet->stream_index == formatData.videoStreamIndex) {
				int ret = avcodec_send_packet(codecData.codecContext, codecData.packet);
				if (ret < 0) {
					av_packet_unref(codecData.packet);
					continue;
				}

				while (ret >= 0) {
					ret = avcodec_receive_frame(codecData.codecContext, codecData.avFrame);
					if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
						break;
					}
					if (ret < 0) {
						break;
					}

					// Calculate frame timestamp
					double const FRAME_TIME =
						codecData.avFrame->pts * av_q2d(formatData.videoStream->time_base);

					// If we've gone past endTime, we're done
					if (FRAME_TIME > endTime + TOLERANCE) {
						eofReached = true;
						break;
					}

					// Check if this frame is close to our target
					double timeDiff = qAbs(FRAME_TIME - TARGET_TIME);
					if (timeDiff < bestTimeDiff) {
						bestTimeDiff = timeDiff;
						bestFrameTime = FRAME_TIME;
						// Convert to RGB
						sws_scale(codecData.swsContext,
							  (const uint8_t *const *)codecData.avFrame->data,
							  codecData.avFrame->linesize, 0,
							  codecData.codecContext->height, codecData.rgbFrame->data,
							  codecData.rgbFrame->linesize);

						// Free previous best frame if exists
						if (bestFrame != nullptr) {
							av_frame_free(&bestFrame);
						}
						bestFrame = av_frame_clone(codecData.rgbFrame);
					}

					// Store last decoded frame for potential reuse
					if (lastDecodedFrame != nullptr) {
						av_frame_free(&lastDecodedFrame);
					}
					sws_scale(codecData.swsContext, (const uint8_t *const *)codecData.avFrame->data,
						  codecData.avFrame->linesize, 0, codecData.codecContext->height,
						  codecData.rgbFrame->data, codecData.rgbFrame->linesize);
					lastDecodedFrame = av_frame_clone(codecData.rgbFrame);
					lastDecodedTime = FRAME_TIME;

					// If we've passed the target time significantly, we're done with this target
					if (FRAME_TIME > TARGET_TIME + TOLERANCE) {
						break;
					}
				}
			}
			av_packet_unref(codecData.packet);

			// If we found a good frame (within tolerance), use it
			if (bestTimeDiff < TOLERANCE) {
				break;
			}

			// If we've gone too far past the target, break
			if (bestFrameTime >= 0.0 && bestFrameTime > TARGET_TIME + FRAME_DURATION) {
				break;
			}
		}

		// Convert best frame to VideoFrame if we found one
		if (bestFrame != nullptr && bestTimeDiff < TOLERANCE * 2 && bestFrameTime >= 0.0) {
			VideoFrame frame;
			frame.timestamp = bestFrameTime;
			frame.frameNumber = frameNumber;

			// Convert to QPixmap (copy image data to ensure it persists)
			QImage image(bestFrame->data[0], codecData.codecContext->width, codecData.codecContext->height,
				     bestFrame->linesize[0], QImage::Format_RGB888);
			frame.pixmap = QPixmap::fromImage(image.copy());
			frames.append(frame);

			av_frame_free(&bestFrame);
			frameNumber++;
		} else if (bestFrame != nullptr) {
			// Free unused best frame
			av_frame_free(&bestFrame);
		}

		// Move to next target time
		targetIndex++;
	}

	// Cleanup last decoded frame
	if (lastDecodedFrame != nullptr) {
		av_frame_free(&lastDecodedFrame);
	}

	// Cleanup
	cleanupCodecContext(codecData);
	cleanupFormatContext(formatData);

	qDebug() << "VideoExtractor::extractFrames: Extracted" << frames.size() << "frames from" << startTime << "to"
		 << endTime << "(expected ~" << targetTimes.size() << "frames)";
	return frames;
}

void VideoExtractor::close()
{
	m_fileOpen = false;
	m_filePath.clear();
}
