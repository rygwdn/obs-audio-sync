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

#ifndef VIDEO_EXTRACTOR_H
#define VIDEO_EXTRACTOR_H

#include <QString>
#include <QPixmap>
#include <QVector>
#include <QPair>

// FFmpeg forward declarations
extern "C" {
struct AVFormatContext;
struct AVStream;
struct AVCodecContext;
struct SwsContext;
struct AVFrame;
struct AVPacket;
}

struct VideoFrame {
	QPixmap pixmap{};
	double timestamp{}; // Time in seconds
	int frameNumber{};  // Frame index
};

class VideoExtractor {
public:
	VideoExtractor();
	~VideoExtractor();

	// Delete copy and move constructors/assignments
	VideoExtractor(const VideoExtractor &) = delete;
	VideoExtractor &operator=(const VideoExtractor &) = delete;
	VideoExtractor(VideoExtractor &&) = delete;
	VideoExtractor &operator=(VideoExtractor &&) = delete;

	// Open video file and prepare for extraction
	bool openFile(const QString &filePath);

	// Extract frame at specific timestamp
	[[nodiscard]] VideoFrame extractFrameAt(double timestamp) const;

	// Extract all frames in time range
	QVector<VideoFrame> extractFrames(double startTime, double endTime);

	// Get video FPS
	[[nodiscard]] double getFPS() const { return m_fps; }

	// Get video duration
	[[nodiscard]] double getDuration() const { return m_duration; }

	// Close file and cleanup
	void close();

private:
	bool initializeFFmpeg();
	void cleanupFFmpeg();

	// Helper functions for extractFrameAt
	struct FormatContextData {
		AVFormatContext *formatContext{};
		int videoStreamIndex{};
		AVStream *videoStream{};
	};

	struct CodecContextData {
		AVCodecContext *codecContext{};
		SwsContext *swsContext{};
		AVFrame *avFrame{};
		AVFrame *rgbFrame{};
		AVPacket *packet{};
		uint8_t *buffer{};
	};

	static FormatContextData setupFormatContext(const QString &filePath);
	static void cleanupFormatContext(FormatContextData &data);
	static CodecContextData setupCodecContext(const FormatContextData &formatData);
	static void cleanupCodecContext(CodecContextData &data);
	static AVFrame *findBestFrame(const FormatContextData &formatData, const CodecContextData &codecData,
				      double timestamp);
	static bool processDecodedFrame(const FormatContextData &formatData, const CodecContextData &codecData,
					AVFrame *decodedFrame, double timestamp, double &bestTimeDiff,
					AVFrame *&currentBestFrame);
	static bool decodeVideoPackets(const FormatContextData &formatData, const CodecContextData &codecData,
				       double timestamp, double &bestTimeDiff, AVFrame *&bestFrame);

	QString m_filePath{};
	double m_fps{};
	double m_duration{};
	bool m_fileOpen{};
};

#endif // VIDEO_EXTRACTOR_H
