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
#include <QFuture>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

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
	if (buffer == nullptr) {
		av_frame_free(&rgbFrame);
		av_frame_free(&avFrame);
		av_packet_free(&packet);
		avcodec_free_context(&codecContext);
		return data;
	}
	av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, buffer, AV_PIX_FMT_RGB24, codecContext->width,
			     codecContext->height, 1);

	// Set frame properties required for cloning
	rgbFrame->format = AV_PIX_FMT_RGB24;
	rgbFrame->width = codecContext->width;
	rgbFrame->height = codecContext->height;

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

double VideoExtractor::calculateFrameDifference(const QPixmap &frame1, const QPixmap &frame2)
{
	if (frame1.isNull() || frame2.isNull()) {
		return -1.0; // Error: invalid frames
	}

	// Downsample frames for faster comparison (1/4 size = 16x faster)
	// Target size: 160x90 (16:9 aspect ratio, ~1/4 of typical 640x360)
	const int TARGET_WIDTH = 160;
	const int TARGET_HEIGHT = 90;

	QPixmap scaled1 = frame1.scaled(TARGET_WIDTH, TARGET_HEIGHT, Qt::KeepAspectRatio, Qt::FastTransformation);
	QPixmap scaled2 = frame2.scaled(TARGET_WIDTH, TARGET_HEIGHT, Qt::KeepAspectRatio, Qt::FastTransformation);

	QImage img1 = scaled1.toImage();
	QImage img2 = scaled2.toImage();

	// Ensure same size after scaling
	if (img1.size() != img2.size()) {
		return -1.0; // Error: size mismatch
	}

	// Calculate SAD (Sum of Absolute Differences)
	qint64 totalDiff = 0;
	int pixelCount = img1.width() * img1.height();

	for (int y = 0; y < img1.height(); y++) {
		for (int x = 0; x < img1.width(); x++) {
			QRgb pixel1 = img1.pixel(x, y);
			QRgb pixel2 = img2.pixel(x, y);

			// Calculate RGB difference
			int rDiff = qAbs(qRed(pixel1) - qRed(pixel2));
			int gDiff = qAbs(qGreen(pixel1) - qGreen(pixel2));
			int bDiff = qAbs(qBlue(pixel1) - qBlue(pixel2));

			totalDiff += (rDiff + gDiff + bDiff);
		}
	}

	// Return average difference per pixel (normalized to 0-255 range)
	return pixelCount > 0 ? (double)totalDiff / pixelCount : 0.0;
}

// static
void VideoExtractor::calculateFrameDifferences(QVector<VideoFrame> &frames)
{
	if (frames.size() < 2) {
		// First frame has no previous frame
		if (!frames.isEmpty()) {
			frames[0].differenceFromPrevious = 0.0;
		}
		return;
	}

	// First frame has no previous frame
	frames[0].differenceFromPrevious = 0.0;

	// Calculate difference for each subsequent frame
	for (int i = 1; i < frames.size(); i++) {
		double diff = calculateFrameDifference(frames[i - 1].pixmap, frames[i].pixmap);
		if (diff < 0.0) {
			// Error calculating difference, use 0.0
			frames[i].differenceFromPrevious = 0.0;
		} else {
			frames[i].differenceFromPrevious = diff;
		}
	}
}

QVector<VideoFrame> VideoExtractor::extractFrames(double startTime, double endTime)
{
	return extractFramesInRange(startTime, endTime);
}

QVector<VideoFrame> VideoExtractor::extractFramesInRange(double startTime, double endTime)
{
	QVector<VideoFrame> frames;

	if (!m_fileOpen || m_filePath.isEmpty() || m_fps <= 0.0) {
		qWarning() << "VideoExtractor::extractFramesInRange: File not open or invalid FPS (m_fileOpen="
			   << m_fileOpen << "m_filePath.isEmpty()=" << m_filePath.isEmpty() << "m_fps=" << m_fps << ")";
		return frames;
	}

	// Setup format context
	FormatContextData formatData = setupFormatContext(m_filePath);
	if (formatData.formatContext == nullptr) {
		qWarning() << "VideoExtractor::extractFramesInRange: Failed to setup format context for" << m_filePath;
		return frames;
	}

	// Setup codec context
	CodecContextData codecData = setupCodecContext(formatData);
	if (codecData.codecContext == nullptr) {
		qWarning() << "VideoExtractor::extractFramesInRange: Failed to setup codec context";
		cleanupFormatContext(formatData);
		return frames;
	}

	// Seek to start time
	double const TIME_BASE = av_q2d(formatData.videoStream->time_base);
	auto seekTarget = (int64_t)(startTime / TIME_BASE);
	int const SEEK_RET =
		av_seek_frame(formatData.formatContext, formatData.videoStreamIndex, seekTarget, AVSEEK_FLAG_BACKWARD);
	if (SEEK_RET < 0) {
		char errbuf[AV_ERROR_MAX_STRING_SIZE];
		av_strerror(SEEK_RET, errbuf, AV_ERROR_MAX_STRING_SIZE);
		qWarning() << "VideoExtractor::extractFramesInRange: Failed to seek to start time" << startTime
			   << "(target=" << seekTarget << "):" << errbuf;
		cleanupCodecContext(codecData);
		cleanupFormatContext(formatData);
		return frames;
	}

	// Flush codec buffers after seek
	avcodec_flush_buffers(codecData.codecContext);

	double const FRAME_DURATION = 1.0 / m_fps;
	double const TOLERANCE = FRAME_DURATION * 0.5; // Half a frame duration tolerance

	// Calculate target frame numbers directly from time range
	// This is more efficient than matching timestamps
	int startFrameNumber = static_cast<int>(qRound(startTime * m_fps));
	int endFrameNumber = static_cast<int>(qRound(endTime * m_fps));
	int numFrames = endFrameNumber - startFrameNumber + 1;

	if (numFrames <= 0) {
		qWarning() << "VideoExtractor::extractFramesInRange: Invalid frame range (startTime=" << startTime
			   << "endTime=" << endTime << "fps=" << m_fps << ")";
		cleanupCodecContext(codecData);
		cleanupFormatContext(formatData);
		return frames;
	}

	// Decode frames sequentially and extract at target frame intervals
	// We decode all frames but only convert to RGB the ones we need
	int currentFrameIndex = 0; // Index in our output array
	int decodedFrameCount = 0; // Total frames decoded (for tracking)
	bool eofReached = false;
	AVFrame *lastDecodedFrame = nullptr;
	double lastDecodedTime = -1.0;

	while (!eofReached && currentFrameIndex < numFrames) {
		int const TARGET_FRAME_NUMBER = startFrameNumber + currentFrameIndex;
		double const TARGET_TIME = startTime + (currentFrameIndex * FRAME_DURATION);

		// Decode frames sequentially - we must decode all frames due to codec dependencies
		// but we only convert to RGB when we have a frame we actually want
		AVFrame *targetFrame = nullptr;
		double targetFrameTime = TARGET_TIME;

		while (!eofReached && targetFrame == nullptr) {
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
						char errbuf[AV_ERROR_MAX_STRING_SIZE];
						av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
						qWarning()
							<< "VideoExtractor::extractFramesInRange: avcodec_receive_frame error:"
							<< errbuf;
						break;
					}
					decodedFrameCount++;

					// Calculate frame timestamp from PTS (or use calculated time if PTS unavailable)
					double frameTime = TARGET_TIME;
					if (codecData.avFrame->pts != AV_NOPTS_VALUE) {
						frameTime = codecData.avFrame->pts * TIME_BASE;
					} else {
						// Fallback: calculate from frame number
						frameTime = startTime + (decodedFrameCount * FRAME_DURATION);
					}

					// If we've gone past endTime, we're done
					if (frameTime > endTime + TOLERANCE) {
						eofReached = true;
						break;
					}

					// Check if this frame matches our target (within tolerance)
					double timeDiff = qAbs(frameTime - TARGET_TIME);
					if (timeDiff < TOLERANCE) {
						// This is the frame we want - convert to RGB (ONLY NOW)
						sws_scale(codecData.swsContext,
							  (const uint8_t *const *)codecData.avFrame->data,
							  codecData.avFrame->linesize, 0,
							  codecData.codecContext->height, codecData.rgbFrame->data,
							  codecData.rgbFrame->linesize);

						targetFrame = av_frame_clone(codecData.rgbFrame);
						targetFrameTime = frameTime;
						break; // Found our frame
					}

					// Store last decoded frame for potential reuse (keep in native format, not RGB)
					if (lastDecodedFrame != nullptr) {
						av_frame_free(&lastDecodedFrame);
					}
					lastDecodedFrame = av_frame_clone(codecData.avFrame);
					lastDecodedTime = frameTime;

					// If we've passed the target significantly, use last frame as fallback
					if (frameTime > TARGET_TIME + FRAME_DURATION && targetFrame == nullptr) {
						// Convert last frame to RGB as fallback
						sws_scale(codecData.swsContext,
							  (const uint8_t *const *)lastDecodedFrame->data,
							  lastDecodedFrame->linesize, 0, codecData.codecContext->height,
							  codecData.rgbFrame->data, codecData.rgbFrame->linesize);
						targetFrame = av_frame_clone(codecData.rgbFrame);
						targetFrameTime = lastDecodedTime;
						break;
					}
				}
			}
			av_packet_unref(codecData.packet);
		}

		// Convert target frame to VideoFrame if we found one
		if (targetFrame != nullptr) {
			VideoFrame frame;
			frame.timestamp = targetFrameTime;
			frame.frameNumber = TARGET_FRAME_NUMBER;

			// Convert to QPixmap (copy image data to ensure it persists)
			QImage image(targetFrame->data[0], codecData.codecContext->width,
				     codecData.codecContext->height, targetFrame->linesize[0], QImage::Format_RGB888);
			frame.pixmap = QPixmap::fromImage(image.copy());
			frames.append(frame);

			av_frame_free(&targetFrame);
			currentFrameIndex++;
		} else {
			// No frame found - can happen near boundaries
			qWarning() << "VideoExtractor::extractFramesInRange: No frame found for target"
				   << TARGET_FRAME_NUMBER << "(time=" << TARGET_TIME << ")";
			currentFrameIndex++;
		}
	}

	// Cleanup last decoded frame
	if (lastDecodedFrame != nullptr) {
		av_frame_free(&lastDecodedFrame);
	}

	// Cleanup
	cleanupCodecContext(codecData);
	cleanupFormatContext(formatData);

	// Calculate frame-to-frame differences
	VideoExtractor::calculateFrameDifferences(frames);

	return frames;
}

QVector<VideoExtractor::NativeFrame> VideoExtractor::decodeFramesToNative(double startTime, double endTime)
{
	QVector<NativeFrame> nativeFrames;

	if (!m_fileOpen || m_filePath.isEmpty() || m_fps <= 0.0) {
		qWarning() << "VideoExtractor::decodeFramesToNative: File not open or invalid FPS";
		return nativeFrames;
	}

	// Setup format context
	FormatContextData formatData = setupFormatContext(m_filePath);
	if (formatData.formatContext == nullptr) {
		qWarning() << "VideoExtractor::decodeFramesToNative: Failed to setup format context";
		return nativeFrames;
	}

	// Setup codec context (no RGB conversion needed yet)
	CodecContextData codecData = setupCodecContext(formatData);
	if (codecData.codecContext == nullptr) {
		qWarning() << "VideoExtractor::decodeFramesToNative: Failed to setup codec context";
		cleanupFormatContext(formatData);
		return nativeFrames;
	}

	// Seek to start time
	double const TIME_BASE = av_q2d(formatData.videoStream->time_base);
	auto seekTarget = (int64_t)(startTime / TIME_BASE);
	int const SEEK_RET =
		av_seek_frame(formatData.formatContext, formatData.videoStreamIndex, seekTarget, AVSEEK_FLAG_BACKWARD);
	if (SEEK_RET < 0) {
		char errbuf[AV_ERROR_MAX_STRING_SIZE];
		av_strerror(SEEK_RET, errbuf, AV_ERROR_MAX_STRING_SIZE);
		qWarning() << "VideoExtractor::decodeFramesToNative: Failed to seek:" << errbuf;
		cleanupCodecContext(codecData);
		cleanupFormatContext(formatData);
		return nativeFrames;
	}

	// Flush codec buffers after seek
	avcodec_flush_buffers(codecData.codecContext);

	double const FRAME_DURATION = 1.0 / m_fps;
	double const TOLERANCE = FRAME_DURATION * 0.5;
	int frameNumber = 0;
	bool eofReached = false;

	// Decode all frames in range to native format (single pass, fast)
	while (!eofReached) {
		int readRet = av_read_frame(formatData.formatContext, codecData.packet);
		if (readRet < 0) {
			eofReached = true;
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
				double frameTime = startTime + (frameNumber * FRAME_DURATION);
				if (codecData.avFrame->pts != AV_NOPTS_VALUE) {
					frameTime = codecData.avFrame->pts * TIME_BASE;
				}

				// If we've gone past endTime, we're done
				if (frameTime > endTime + TOLERANCE) {
					eofReached = true;
					break;
				}

				// Only store frames within our range
				if (frameTime >= startTime - TOLERANCE && frameTime <= endTime + TOLERANCE) {
					NativeFrame nativeFrame;
					nativeFrame.frame = av_frame_clone(codecData.avFrame);
					nativeFrame.timestamp = frameTime;
					nativeFrame.frameNumber = frameNumber;
					nativeFrames.append(nativeFrame);
				}

				frameNumber++;
			}
		}
		av_packet_unref(codecData.packet);
	}

	// Cleanup (but keep native frames - they'll be freed when converted)
	cleanupCodecContext(codecData);
	cleanupFormatContext(formatData);

	return nativeFrames;
}

VideoFrame VideoExtractor::convertSingleNativeFrameToRGB(const NativeFrame &nativeFrame, AVCodecContext *codecContext,
							 SwsContext *swsContext)
{
	VideoFrame videoFrame;
	videoFrame.timestamp = nativeFrame.timestamp;
	videoFrame.frameNumber = nativeFrame.frameNumber;

	if (nativeFrame.frame == nullptr || codecContext == nullptr || swsContext == nullptr) {
		return videoFrame;
	}

	// Allocate RGB frame for conversion
	AVFrame *rgbFrame = av_frame_alloc();
	if (rgbFrame == nullptr) {
		return videoFrame;
	}

	int const NUM_BYTES = av_image_get_buffer_size(AV_PIX_FMT_RGB24, codecContext->width, codecContext->height, 1);
	uint8_t *buffer = (uint8_t *)av_malloc(NUM_BYTES * sizeof(uint8_t));
	if (buffer == nullptr) {
		av_frame_free(&rgbFrame);
		return videoFrame;
	}

	av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, buffer, AV_PIX_FMT_RGB24, codecContext->width,
			     codecContext->height, 1);
	rgbFrame->format = AV_PIX_FMT_RGB24;
	rgbFrame->width = codecContext->width;
	rgbFrame->height = codecContext->height;

	// Convert to RGB
	sws_scale(swsContext, (const uint8_t *const *)nativeFrame.frame->data, nativeFrame.frame->linesize, 0,
		  codecContext->height, rgbFrame->data, rgbFrame->linesize);

	// Convert to QPixmap
	QImage image(rgbFrame->data[0], codecContext->width, codecContext->height, rgbFrame->linesize[0],
		     QImage::Format_RGB888);
	videoFrame.pixmap = QPixmap::fromImage(image.copy());

	// Cleanup
	av_free(buffer);
	av_frame_free(&rgbFrame);

	return videoFrame;
}

QVector<VideoFrame> VideoExtractor::extractFramesOptimized(double startTime, double endTime, double cursorPosition)
{
	QVector<VideoFrame> frames;

	if (!m_fileOpen || m_filePath.isEmpty() || m_fps <= 0.0) {
		return frames;
	}

	// Phase 1: Decode all frames to native format (single pass, fast)
	QVector<NativeFrame> nativeFrames = decodeFramesToNative(startTime, endTime);
	if (nativeFrames.isEmpty()) {
		return frames;
	}

	// Phase 2: Convert to RGB in parallel, starting from cursor
	// We need codec context info for conversion
	FormatContextData formatData = setupFormatContext(m_filePath);
	if (formatData.formatContext == nullptr) {
		// Cleanup native frames
		for (auto &nf : nativeFrames) {
			if (nf.frame) {
				av_frame_free(&nf.frame);
			}
		}
		return frames;
	}

	CodecContextData codecData = setupCodecContext(formatData);
	if (codecData.codecContext == nullptr) {
		cleanupFormatContext(formatData);
		// Cleanup native frames
		for (auto &nf : nativeFrames) {
			if (nf.frame) {
				av_frame_free(&nf.frame);
			}
		}
		return frames;
	}

	// Convert native frames to RGB
	frames = convertNativeFramesToRGB(nativeFrames, codecData.codecContext, codecData.codecContext->width,
					  codecData.codecContext->height, codecData.codecContext->pix_fmt,
					  cursorPosition);

	// Cleanup native frames
	for (auto &nf : nativeFrames) {
		if (nf.frame) {
			av_frame_free(&nf.frame);
		}
	}

	cleanupCodecContext(codecData);
	cleanupFormatContext(formatData);

	return frames;
}

QVector<VideoFrame> VideoExtractor::convertNativeFramesToRGB(const QVector<NativeFrame> &nativeFrames,
							     AVCodecContext *codecContext, int width, int height,
							     AVPixelFormat srcFormat, double cursorPosition)
{
	QVector<VideoFrame> frames;

	if (nativeFrames.isEmpty() || codecContext == nullptr) {
		return frames;
	}

	// Sort frames by distance from cursor (priority zone first)
	QVector<QPair<int, double>> frameDistances; // index, distance
	for (int i = 0; i < nativeFrames.size(); i++) {
		double distance = qAbs(nativeFrames[i].timestamp - cursorPosition);
		frameDistances.append(qMakePair(i, distance));
	}

	std::sort(frameDistances.begin(), frameDistances.end(),
		  [](const QPair<int, double> &a, const QPair<int, double> &b) { return a.second < b.second; });

	// Convert frames in parallel, starting from cursor and moving outward
	// Use QtConcurrent to parallelize RGB conversion
	QVector<QPair<int, VideoFrame>> convertedFrames;

	// Convert frames in batches: priority zone first (synchronous), then rest (parallel)
	const double PRIORITY_WINDOW = 1.0; // 1 second on each side
	QVector<int> priorityIndices;
	QVector<int> remainingIndices;

	for (const auto &pair : frameDistances) {
		if (pair.second <= PRIORITY_WINDOW) {
			priorityIndices.append(pair.first);
		} else {
			remainingIndices.append(pair.first);
		}
	}

	// Create a SwsContext for priority zone (main thread)
	SwsContext *mainSwsContext = sws_getContext(width, height, srcFormat, width, height, AV_PIX_FMT_RGB24,
						    SWS_BILINEAR, nullptr, nullptr, nullptr);
	if (mainSwsContext == nullptr) {
		return frames;
	}

	// Convert priority frames first (synchronous, for immediate UI feedback)
	for (int idx : priorityIndices) {
		VideoFrame frame = convertSingleNativeFrameToRGB(nativeFrames[idx], codecContext, mainSwsContext);
		convertedFrames.append(qMakePair(idx, frame));
	}

	sws_freeContext(mainSwsContext);

	// Convert remaining frames in parallel
	if (!remainingIndices.isEmpty()) {
		QVector<QFuture<QPair<int, VideoFrame>>> futures;
		for (int idx : remainingIndices) {
			// Convert in parallel - create SwsContext inside the thread (it's not thread-safe)
			QFuture<QPair<int, VideoFrame>> future = QtConcurrent::run([&nativeFrames, idx, codecContext,
										    width, height, srcFormat]() {
				// Create SwsContext for this thread
				SwsContext *threadSwsContext = sws_getContext(width, height, srcFormat, width, height,
									      AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr,
									      nullptr, nullptr);
				if (threadSwsContext == nullptr) {
					return qMakePair(idx, VideoFrame{});
				}

				VideoFrame frame = convertSingleNativeFrameToRGB(nativeFrames[idx], codecContext,
										 threadSwsContext);

				// Cleanup thread-local SwsContext
				sws_freeContext(threadSwsContext);

				return qMakePair(idx, frame);
			});
			futures.append(future);
		}

		// Wait for all conversions to complete
		for (auto &future : futures) {
			future.waitForFinished();
			convertedFrames.append(future.result());
		}
	}

	// Sort by original index to maintain timestamp order
	std::sort(convertedFrames.begin(), convertedFrames.end(),
		  [](const QPair<int, VideoFrame> &a, const QPair<int, VideoFrame> &b) { return a.first < b.first; });

	// Extract frames in order
	for (const auto &pair : convertedFrames) {
		frames.append(pair.second);
	}

	// Calculate frame differences
	calculateFrameDifferences(frames);

	return frames;
}

void VideoExtractor::close()
{
	m_fileOpen = false;
	m_filePath.clear();
}
