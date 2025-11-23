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
#include "libavcodec/codec_par.h"
#include "libavcodec/codec.h"
#include "libavutil/frame.h"
#include "libavcodec/packet.h"
#include "libavutil/pixfmt.h"
#include "libavutil/error.h"
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

	AVFormatContext const *formatContext = nullptr;
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

VideoFrame VideoExtractor::extractFrameAt(double timestamp) const
{
	VideoFrame frame;
	frame.timestamp = timestamp;
	frame.frameNumber = -1;

	if (!m_fileOpen || m_filePath.isEmpty()) {
		return frame;
	}

	AVFormatContext const *formatContext = nullptr;
	int ret = avformat_open_input(&formatContext, m_filePath.toUtf8().constData(), nullptr, nullptr);
	if (ret < 0) {
		return frame;
	}

	ret = avformat_find_stream_info(formatContext, nullptr);
	if (ret < 0) {
		avformat_close_input(&formatContext);
		return frame;
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
		return frame;
	}

	AVCodecParameters const *codecParams = formatContext->streams[videoStreamIndex]->codecpar;
	const AVCodec *codec = avcodec_find_decoder(codecParams->codec_id);
	if (codec == nullptr) {
		avformat_close_input(&formatContext);
		return frame;
	}

	AVCodecContext *codecContext = avcodec_alloc_context3(codec);
	if (codecContext == nullptr) {
		avformat_close_input(&formatContext);
		return frame;
	}

	ret = avcodec_parameters_to_context(codecContext, codecParams);
	if (ret < 0) {
		avcodec_free_context(&codecContext);
		avformat_close_input(&formatContext);
		return frame;
	}

	ret = avcodec_open2(codecContext, codec, nullptr);
	if (ret < 0) {
		avcodec_free_context(&codecContext);
		avformat_close_input(&formatContext);
		return frame;
	}

	// Seek to timestamp
	AVStream const *videoStream = formatContext->streams[videoStreamIndex];
	auto seekTarget = (int64_t)(timestamp / av_q2d(videoStream->time_base));
	ret = av_seek_frame(formatContext, videoStreamIndex, seekTarget, AVSEEK_FLAG_BACKWARD);
	if (ret < 0) {
		avcodec_free_context(&codecContext);
		avformat_close_input(&formatContext);
		return frame;
	}

	// Allocate frame for decoding
	AVFrame *avFrame = av_frame_alloc();
	AVFrame *rgbFrame = av_frame_alloc();
	AVPacket *packet = av_packet_alloc();

	// Allocate buffer for RGB frame
	int const NUM_BYTES = avImageGetBufferSize(AV_PIX_FMT_RGB24, codecContext->width, codecContext->height, 1);
	auto *buffer = (uint8_t *)av_malloc(NUM_BYTES * sizeof(uint8_t));
	avImageFillArrays(rgbFrame->data, rgbFrame->linesize, buffer, AV_PIX_FMT_RGB24, codecContext->width,
			  codecContext->height, 1);

	// Create sws context for conversion
	SwsContext *swsContext = sws_getContext(codecContext->width, codecContext->height, codecContext->pix_fmt,
						codecContext->width, codecContext->height, AV_PIX_FMT_RGB24,
						SWS_BILINEAR, nullptr, nullptr, nullptr);

	// Read and decode frames until we find the one closest to timestamp
	double bestTimeDiff = 1e10;
	AVFrame const *bestFrame = nullptr;

	while (av_read_frame(formatContext, packet) >= 0) {
		if (packet->stream_index == videoStreamIndex) {
			ret = avcodec_send_packet(codecContext, packet);
			if (ret < 0) {
				av_packet_unref(packet);
				continue;
			}

			while (ret >= 0) {
				ret = avcodec_receive_frame(codecContext, avFrame);
				if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
					break;
				}
				if (ret < 0) {
					break;
				}

				double frameTime = avFrame->pts * av_q2d(videoStream->time_base);
				double timeDiff = qAbs(frameTime - timestamp);

				if (timeDiff < bestTimeDiff) {
					bestTimeDiff = timeDiff;
					// Convert to RGB
					swsScale(swsContext, (const uint8_t *const *)avFrame->data, avFrame->linesize,
						 0, codecContext->height, rgbFrame->data, rgbFrame->linesize);

					// Copy frame data
					if (bestFrame != nullptr) {
						av_frame_free(&bestFrame);
					}
					bestFrame = av_frame_clone(rgbFrame);
				}

				// If we've passed the timestamp, stop
				if (frameTime > timestamp + 0.1) {
					break;
				}
			}
		}
		av_packet_unref(packet);

		if (bestTimeDiff < 0.05) { // Close enough (50ms)
			break;
		}
	}

	// Convert to QPixmap
	if (bestFrame != nullptr) {
		QImage image(bestFrame->data[0], codecContext->width, codecContext->height, bestFrame->linesize[0],
			     QImage::Format_RGB888);
		frame.pixmap = QPixmap::fromImage(image);
		frame.frameNumber = (int)(timestamp * m_fps);
		av_frame_free(&bestFrame);
	}

	// Cleanup
	swsFreeContext(swsContext);
	av_free(buffer);
	av_frame_free(&rgbFrame);
	av_frame_free(&avFrame);
	av_packet_free(&packet);
	avcodec_free_context(&codecContext);
	avformat_close_input(&formatContext);

	return frame;
}

QVector<VideoFrame> VideoExtractor::extractFrames(double startTime, double endTime) const
{
	QVector<VideoFrame> frames;

	if (!m_fileOpen || m_fps <= 0.0) {
		return frames;
	}

	double const FRAME_DURATION = 1.0 / m_fps;
	double currentTime = startTime;

	while (currentTime <= endTime) {
		VideoFrame frame = extractFrameAt(currentTime);
		if (!frame.pixmap.isNull()) {
			frames.append(frame);
		}
		currentTime += FRAME_DURATION;
	}

	return frames;
}

void VideoExtractor::close()
{
	m_fileOpen = false;
	m_filePath.clear();
}
