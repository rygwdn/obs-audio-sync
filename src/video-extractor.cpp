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
#include "libavutil/mem.h"
#include "libavutil/pixfmt.h"
#include "libavutil/error.h"
#include "libavutil/rational.h"
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
	int ret = avformatOpenInput(&formatContext, filePath.toUtf8().constData(), nullptr, nullptr);
	if (ret < 0) {
		qWarning() << "Could not open video file:" << filePath;
		return false;
	}

	ret = avformatFindStreamInfo(formatContext, nullptr);
	if (ret < 0) {
		avformatCloseInput(&formatContext);
		return false;
	}

	// Find video stream
	int videoStreamIndex = -1;
	for (unsigned int i = 0; i < formatContext->nbStreams; i++) {
		if (formatContext->streams[i]->codecpar->codecType == AVMEDIA_TYPE_VIDEO) {
			videoStreamIndex = static_cast<int>(i);
			break;
		}
	}

	if (videoStreamIndex == -1) {
		qWarning() << "No video stream found";
		avformatCloseInput(&formatContext);
		return false;
	}

	// Get FPS and duration
	AVStream const *videoStream = formatContext->streams[videoStreamIndex];
	if (videoStream->rFrameRate.num > 0 && videoStream->rFrameRate.den > 0) {
		m_fps = avQ2d(videoStream->rFrameRate);
	} else {
		m_fps = 30.0; // Default
	}

	if (formatContext->duration != AV_NOPTS_VALUE) {
		m_duration = (double)formatContext->duration / AV_TIME_BASE;
	}

	avformatCloseInput(&formatContext);
	m_fileOpen = true;

	return true;
}

VideoExtractor::FormatContextData VideoExtractor::setupFormatContext(const QString &filePath)
{
	FormatContextData data{};

	AVFormatContext *formatContext = nullptr;
	int ret = avformatOpenInput(&formatContext, filePath.toUtf8().constData(), nullptr, nullptr);
	if (ret < 0) {
		return data;
	}

	ret = avformatFindStreamInfo(formatContext, nullptr);
	if (ret < 0) {
		avformatCloseInput(&formatContext);
		return data;
	}

	// Find video stream
	int videoStreamIndex = -1;
	for (unsigned int i = 0; i < formatContext->nbStreams; i++) {
		if (formatContext->streams[i]->codecpar->codecType == AVMEDIA_TYPE_VIDEO) {
			videoStreamIndex = static_cast<int>(i);
			break;
		}
	}

	if (videoStreamIndex == -1) {
		avformatCloseInput(&formatContext);
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
		avformatCloseInput(&data.formatContext);
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
	const AVCodec *codec = avcodecFindDecoder(codecParams->codecId);
	if (codec == nullptr) {
		return data;
	}

	AVCodecContext *codecContext = avcodecAllocContext3(codec);
	if (codecContext == nullptr) {
		return data;
	}

	int ret = avcodecParametersToContext(codecContext, codecParams);
	if (ret < 0) {
		avcodecFreeContext(&codecContext);
		return data;
	}

	ret = avcodecOpen2(codecContext, codec, nullptr);
	if (ret < 0) {
		avcodecFreeContext(&codecContext);
		return data;
	}

	// Allocate frame for decoding
	AVFrame *avFrame = avFrameAlloc();
	AVFrame *rgbFrame = avFrameAlloc();
	AVPacket *packet = avPacketAlloc();

	// Allocate buffer for RGB frame
	int const NUM_BYTES = avImageGetBufferSize(AV_PIX_FMT_RGB24, codecContext->width, codecContext->height, 1);
	auto *buffer = (uint8_t *)avMalloc(NUM_BYTES * sizeof(uint8_t));
	avImageFillArrays(rgbFrame->data, rgbFrame->linesize, buffer, AV_PIX_FMT_RGB24, codecContext->width,
			  codecContext->height, 1);

	// Create sws context for conversion
	SwsContext *swsContext = swsGetContext(codecContext->width, codecContext->height, codecContext->pixFmt,
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
		swsFreeContext(data.swsContext);
	}
	if (data.buffer != nullptr) {
		avFree(data.buffer);
	}
	if (data.rgbFrame != nullptr) {
		avFrameFree(&data.rgbFrame);
	}
	if (data.avFrame != nullptr) {
		avFrameFree(&data.avFrame);
	}
	if (data.packet != nullptr) {
		avPacketFree(&data.packet);
	}
	if (data.codecContext != nullptr) {
		avcodecFreeContext(&data.codecContext);
	}
	data = CodecContextData{};
}

bool VideoExtractor::processDecodedFrame(const FormatContextData &formatData, const CodecContextData &codecData,
					 AVFrame *decodedFrame, double timestamp, double &bestTimeDiff,
					 AVFrame *&currentBestFrame)
{
	double const FRAME_TIME = decodedFrame->pts * avQ2d(formatData.videoStream->timeBase);
	double timeDiff = qAbs(FRAME_TIME - timestamp);

	if (timeDiff < bestTimeDiff) {
		bestTimeDiff = timeDiff;
		// Convert to RGB
		swsScale(codecData.swsContext, (const uint8_t *const *)decodedFrame->data, decodedFrame->linesize, 0,
			 codecData.codecContext->height, codecData.rgbFrame->data, codecData.rgbFrame->linesize);

		// Copy frame data
		if (currentBestFrame != nullptr) {
			avFrameFree(&currentBestFrame);
		}
		currentBestFrame = avFrameClone(codecData.rgbFrame);
	}

	// If we've passed the timestamp, stop processing
	return FRAME_TIME <= timestamp + 0.1;
}

bool VideoExtractor::decodeVideoPackets(const FormatContextData &formatData, const CodecContextData &codecData,
					double timestamp, double &bestTimeDiff, AVFrame *&bestFrame)
{
	int ret = avcodecSendPacket(codecData.codecContext, codecData.packet);
	if (ret < 0) {
		return true; // Continue reading packets
	}

	while (ret >= 0) {
		ret = avcodecReceiveFrame(codecData.codecContext, codecData.avFrame);
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
	auto seekTarget = (int64_t)(timestamp / avQ2d(formatData.videoStream->timeBase));
	int const RET =
		avSeekFrame(formatData.formatContext, formatData.videoStreamIndex, seekTarget, AVSEEK_FLAG_BACKWARD);
	if (RET < 0) {
		return nullptr;
	}

	// Read and decode frames until we find the one closest to timestamp
	double const bestTimeDiff = 1e10;
	AVFrame const *bestFrame = nullptr;

	while (avReadFrame(formatData.formatContext, codecData.packet) >= 0) {
		if (codecData.packet->streamIndex == formatData.videoStreamIndex) {
			bool const SHOULD_CONTINUE =
				decodeVideoPackets(formatData, codecData, timestamp, bestTimeDiff, bestFrame);
			if (!SHOULD_CONTINUE) {
				avPacketUnref(codecData.packet);
				break;
			}
		}
		avPacketUnref(codecData.packet);

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

	AVFrame const *bestFrame = findBestFrame(formatData, codecData, timestamp);

	// Convert to QPixmap
	if (bestFrame != nullptr) {
		QImage image(bestFrame->data[0], codecData.codecContext->width, codecData.codecContext->height,
			     bestFrame->linesize[0], QImage::Format_RGB888);
		frame.pixmap = QPixmap::fromImage(image);
		frame.frameNumber = (int)(timestamp * m_fps);
		avFrameFree(&bestFrame);
	}

	// Cleanup
	cleanupCodecContext(codecData);
	cleanupFormatContext(formatData);

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
