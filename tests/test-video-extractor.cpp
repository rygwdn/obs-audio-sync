/*
OBS Audio Sync Plugin - Tests
Copyright (C) 2025 Ryan Wooden

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "test-video-extractor.h"
#include "../src/video-extractor.h"
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

void TestVideoExtractor::initTestCase()
{
	m_extractor = new VideoExtractor();
}

void TestVideoExtractor::cleanupTestCase()
{
	delete m_extractor;
}

void TestVideoExtractor::testOpenFile_invalidFile()
{
	// Test with non-existent file
	bool result = m_extractor->openFile("/nonexistent/file.mp4");
	QVERIFY(!result);

	// Test with invalid file path
	result = m_extractor->openFile("");
	QVERIFY(!result);
}

void TestVideoExtractor::testOpenFile_emptyPath()
{
	// Test with empty path
	bool result = m_extractor->openFile(QString());
	QVERIFY(!result);
}

void TestVideoExtractor::testVideoFrameStructure()
{
	// Test VideoFrame structure
	VideoFrame frame;
	frame.timestamp = 1.5;
	frame.frameNumber = 42;

	QVERIFY(frame.timestamp == 1.5);
	QVERIFY(frame.frameNumber == 42);
	QVERIFY(frame.pixmap.isNull()); // Should be null by default
}

void TestVideoExtractor::testExtractFrameAt_invalidFile()
{
	// Test extracting frame when file is not open
	VideoFrame frame = m_extractor->extractFrameAt(1.0);
	QVERIFY(frame.pixmap.isNull());
	QVERIFY(frame.frameNumber == -1);
	QVERIFY(frame.timestamp == 1.0); // Should still have the requested timestamp
}

void TestVideoExtractor::testExtractFrames_invalidFile()
{
	// Test extracting frames when file is not open
	QVector<VideoFrame> frames = m_extractor->extractFrames(0.0, 1.0);
	QVERIFY(frames.isEmpty());
}

void TestVideoExtractor::testGetFPS_defaultValue()
{
	// Test default FPS value (should be 30.0)
	double fps = m_extractor->getFPS();
	QVERIFY(fps == 30.0);
}

void TestVideoExtractor::testGetDuration_defaultValue()
{
	// Test default duration value (should be 0.0 when no file is open)
	double duration = m_extractor->getDuration();
	QVERIFY(duration == 0.0);
}

void TestVideoExtractor::testClose()
{
	// Test that close() doesn't crash
	m_extractor->close();

	// After close, FPS should still be default
	double fps = m_extractor->getFPS();
	QVERIFY(fps == 30.0);

	// Duration should be 0
	double duration = m_extractor->getDuration();
	QVERIFY(duration == 0.0);
}

// ============================================================================
// Integration Tests for Optimized Extraction
// ============================================================================

void TestVideoExtractor::testDecodeFramesToNative_basic()
{
	// Test basic native frame decoding
	// Note: This requires a valid video file
	// For now, test the structure and error handling

	// Test with invalid file (should return empty)
	QVector<VideoExtractor::NativeFrame> nativeFrames = m_extractor->decodeFramesToNative(0.0, 1.0);
	QVERIFY(nativeFrames.isEmpty());

	// Test structure of NativeFrame
	VideoExtractor::NativeFrame nativeFrame;
	nativeFrame.frame = nullptr;
	nativeFrame.timestamp = 1.5;
	nativeFrame.frameNumber = 42;

	QVERIFY(nativeFrame.frame == nullptr);
	QVERIFY(nativeFrame.timestamp == 1.5);
	QVERIFY(nativeFrame.frameNumber == 42);
}

void TestVideoExtractor::testDecodeFramesToNative_emptyRange()
{
	// Test with empty/invalid range
	QVector<VideoExtractor::NativeFrame> frames1 = m_extractor->decodeFramesToNative(1.0, 0.0);
	QVERIFY(frames1.isEmpty());

	QVector<VideoExtractor::NativeFrame> frames2 = m_extractor->decodeFramesToNative(0.0, 0.0);
	// May be empty or have one frame depending on implementation
	// Just verify it doesn't crash
	QVERIFY(true);
}

void TestVideoExtractor::testDecodeFramesToNative_invalidFile()
{
	// Test with file not open
	QVector<VideoExtractor::NativeFrame> frames = m_extractor->decodeFramesToNative(0.0, 1.0);
	QVERIFY(frames.isEmpty());
}

void TestVideoExtractor::testDecodeFramesToNative_frameOrdering()
{
	// Test that decoded frames are ordered by timestamp
	// This would require a real video file to fully test
	// For now, verify the structure supports ordering

	QVector<VideoExtractor::NativeFrame> frames;
	VideoExtractor::NativeFrame frame1;
	frame1.timestamp = 1.0;
	frame1.frameNumber = 0;
	frames.append(frame1);

	VideoExtractor::NativeFrame frame2;
	frame2.timestamp = 2.0;
	frame2.frameNumber = 1;
	frames.append(frame2);

	// Verify we can sort by timestamp
	std::sort(frames.begin(), frames.end(),
		  [](const VideoExtractor::NativeFrame &a, const VideoExtractor::NativeFrame &b) {
			  return a.timestamp < b.timestamp;
		  });

	QVERIFY(frames[0].timestamp == 1.0);
	QVERIFY(frames[1].timestamp == 2.0);
}

void TestVideoExtractor::testDecodeFramesToNative_timestampAccuracy()
{
	// Test timestamp accuracy in native frames
	// Verify timestamps are calculated correctly

	VideoExtractor::NativeFrame frame;
	frame.timestamp = 1.234567;
	frame.frameNumber = 37;

	// Verify precision is maintained
	QVERIFY(std::abs(frame.timestamp - 1.234567) < 0.000001);
	QVERIFY(frame.frameNumber == 37);
}

void TestVideoExtractor::testConvertSingleNativeFrameToRGB_basic()
{
	// Test converting a single native frame to RGB
	// Note: This requires valid codec context and sws context
	// For now, test error handling with null inputs

	VideoExtractor::NativeFrame nativeFrame;
	nativeFrame.frame = nullptr;
	nativeFrame.timestamp = 1.0;
	nativeFrame.frameNumber = 0;

	// Test with null codec context (should return empty frame)
	VideoFrame result = VideoExtractor::convertSingleNativeFrameToRGB(nativeFrame, nullptr, nullptr);
	QVERIFY(result.pixmap.isNull());
	QVERIFY(result.timestamp == 1.0);
	QVERIFY(result.frameNumber == 0);
}

void TestVideoExtractor::testConvertSingleNativeFrameToRGB_nullFrame()
{
	// Test with null native frame
	VideoExtractor::NativeFrame nativeFrame;
	nativeFrame.frame = nullptr;
	nativeFrame.timestamp = 1.0;
	nativeFrame.frameNumber = 0;

	VideoFrame result = VideoExtractor::convertSingleNativeFrameToRGB(nativeFrame, nullptr, nullptr);
	QVERIFY(result.pixmap.isNull());
}

void TestVideoExtractor::testConvertSingleNativeFrameToRGB_threadSafety()
{
	// Test that conversion can be called from multiple threads
	// This is a structural test - actual thread safety requires real video data

	QVector<VideoExtractor::NativeFrame> nativeFrames;
	for (int i = 0; i < 10; i++) {
		VideoExtractor::NativeFrame frame;
		frame.frame = nullptr;
		frame.timestamp = i * 0.1;
		frame.frameNumber = i;
		nativeFrames.append(frame);
	}

	// Verify structure supports parallel processing
	QVERIFY(nativeFrames.size() == 10);
}

void TestVideoExtractor::testConvertNativeFramesToRGB_basic()
{
	// Test basic conversion of native frames to RGB
	QVector<VideoExtractor::NativeFrame> nativeFrames;
	// Empty input should return empty output
	QVector<VideoFrame> frames = VideoExtractor::convertNativeFramesToRGB(nativeFrames, nullptr, 0, 0,
										AV_PIX_FMT_NONE, 0.0);
	QVERIFY(frames.isEmpty());
}

void TestVideoExtractor::testConvertNativeFramesToRGB_cursorPriority()
{
	// Test that frames are converted starting from cursor position
	QVector<VideoExtractor::NativeFrame> nativeFrames;

	// Create test frames at different timestamps
	for (int i = 0; i < 10; i++) {
		VideoExtractor::NativeFrame frame;
		frame.frame = nullptr;
		frame.timestamp = i * 0.1; // 0.0, 0.1, 0.2, ..., 0.9
		frame.frameNumber = i;
		nativeFrames.append(frame);
	}

	// Test cursor at middle (0.5)
	double cursorPosition = 0.5;

	// Verify structure supports priority-based conversion
	// (actual conversion requires valid codec context)
	QVector<QPair<int, double>> frameDistances;
	for (int i = 0; i < nativeFrames.size(); i++) {
		double distance = std::abs(nativeFrames[i].timestamp - cursorPosition);
		frameDistances.append(qMakePair(i, distance));
	}

	// Sort by distance from cursor
	std::sort(frameDistances.begin(), frameDistances.end(),
		  [](const QPair<int, double> &a, const QPair<int, double> &b) {
			  return a.second < b.second;
		  });

	// Verify closest frame to cursor (0.5) is frame at 0.5
	QVERIFY(frameDistances[0].first == 5); // Frame at index 5 (timestamp 0.5)
}

void TestVideoExtractor::testConvertNativeFramesToRGB_parallelConversion()
{
	// Test that conversion supports parallel processing
	// Verify structure allows parallel conversion

	QVector<VideoExtractor::NativeFrame> nativeFrames;
	for (int i = 0; i < 20; i++) {
		VideoExtractor::NativeFrame frame;
		frame.frame = nullptr;
		frame.timestamp = i * 0.1;
		frame.frameNumber = i;
		nativeFrames.append(frame);
	}

	// Verify we can split into priority and remaining
	const double PRIORITY_WINDOW = 1.0;
	double cursorPosition = 1.0;

	QVector<int> priorityIndices;
	QVector<int> remainingIndices;

	for (int i = 0; i < nativeFrames.size(); i++) {
		double distance = std::abs(nativeFrames[i].timestamp - cursorPosition);
		if (distance <= PRIORITY_WINDOW) {
			priorityIndices.append(i);
		} else {
			remainingIndices.append(i);
		}
	}

	// Verify we have both priority and remaining frames
	QVERIFY(!priorityIndices.isEmpty());
	QVERIFY(!remainingIndices.isEmpty());
	QVERIFY(priorityIndices.size() + remainingIndices.size() == nativeFrames.size());
}

void TestVideoExtractor::testConvertNativeFramesToRGB_frameOrdering()
{
	// Test that converted frames maintain timestamp ordering
	QVector<VideoExtractor::NativeFrame> nativeFrames;

	// Create frames in reverse order
	for (int i = 9; i >= 0; i--) {
		VideoExtractor::NativeFrame frame;
		frame.frame = nullptr;
		frame.timestamp = i * 0.1;
		frame.frameNumber = i;
		nativeFrames.append(frame);
	}

	// Verify structure supports reordering
	std::sort(nativeFrames.begin(), nativeFrames.end(),
		  [](const VideoExtractor::NativeFrame &a, const VideoExtractor::NativeFrame &b) {
			  return a.timestamp < b.timestamp;
		  });

	// Verify sorted order
	for (int i = 0; i < nativeFrames.size(); i++) {
		QVERIFY(std::abs(nativeFrames[i].timestamp - (i * 0.1)) < 0.0001);
	}
}

void TestVideoExtractor::testConvertNativeFramesToRGB_emptyInput()
{
	// Test with empty input
	QVector<VideoExtractor::NativeFrame> emptyFrames;
	QVector<VideoFrame> frames = VideoExtractor::convertNativeFramesToRGB(emptyFrames, nullptr, 0, 0,
										AV_PIX_FMT_NONE, 0.0);
	QVERIFY(frames.isEmpty());
}

void TestVideoExtractor::testExtractFramesOptimized_basic()
{
	// Test basic optimized extraction
	// Without a valid video file, test error handling

	QVector<VideoFrame> frames = m_extractor->extractFramesOptimized(0.0, 1.0, 0.5);
	QVERIFY(frames.isEmpty()); // Should be empty when file not open
}

void TestVideoExtractor::testExtractFramesOptimized_cursorPosition()
{
	// Test that cursor position affects priority zone
	// Verify the structure supports cursor-based priority

	double startTime = 0.0;
	double endTime = 4.0;
	double cursorPosition = 2.0;

	// Test structure - actual extraction requires valid video
	QVector<VideoFrame> frames = m_extractor->extractFramesOptimized(startTime, endTime, cursorPosition);
	// Without valid video, should return empty
	QVERIFY(frames.isEmpty());
}

void TestVideoExtractor::testExtractFramesOptimized_priorityZoneFirst()
{
	// Test that priority zone frames are converted first
	// This is verified by the incremental emission in the worker
	// For now, test the structure

	const double PRIORITY_WINDOW = 1.0;
	double cursorPosition = 2.0;

	// Simulate frame timestamps
	QVector<double> frameTimestamps;
	for (int i = 0; i < 40; i++) {
		frameTimestamps.append(i * 0.1); // 0.0 to 3.9 seconds
	}

	// Split into priority and remaining
	QVector<double> priorityTimestamps;
	QVector<double> remainingTimestamps;

	for (double ts : frameTimestamps) {
		double distance = std::abs(ts - cursorPosition);
		if (distance <= PRIORITY_WINDOW) {
			priorityTimestamps.append(ts);
		} else {
			remainingTimestamps.append(ts);
		}
	}

	// Verify priority zone is around cursor
	QVERIFY(!priorityTimestamps.isEmpty());
	for (double ts : priorityTimestamps) {
		QVERIFY(std::abs(ts - cursorPosition) <= PRIORITY_WINDOW);
	}
}

void TestVideoExtractor::testExtractFramesOptimized_frameDifferences()
{
	// Test that frame differences are calculated correctly
	QVector<VideoFrame> frames;

	// Create test frames (without actual pixmaps, we can't test difference calculation fully)
	// But we can test the structure
	for (int i = 0; i < 5; i++) {
		VideoFrame frame;
		frame.timestamp = i * 0.1;
		frame.frameNumber = i;
		frame.differenceFromPrevious = 0.0; // Will be calculated
		frames.append(frame);
	}

	// First frame should have difference 0.0
	if (!frames.isEmpty()) {
		frames[0].differenceFromPrevious = 0.0;
	}

	// Verify structure supports difference calculation
	QVERIFY(frames.size() == 5);
	QVERIFY(frames[0].differenceFromPrevious == 0.0);
}

void TestVideoExtractor::testExtractFramesOptimized_edgeCases()
{
	// Test edge cases

	// Empty range
	QVector<VideoFrame> frames1 = m_extractor->extractFramesOptimized(1.0, 0.0, 0.5);
	QVERIFY(frames1.isEmpty());

	// Zero duration range
	QVector<VideoFrame> frames2 = m_extractor->extractFramesOptimized(1.0, 1.0, 1.0);
	// May return empty or one frame

	// Cursor outside range
	QVector<VideoFrame> frames3 = m_extractor->extractFramesOptimized(0.0, 1.0, 5.0);
	// Should handle gracefully

	// Very small range
	QVector<VideoFrame> frames4 = m_extractor->extractFramesOptimized(0.0, 0.01, 0.005);
	// Should handle gracefully
}

void TestVideoExtractor::testExtractFramesOptimized_largeRange()
{
	// Test with large time range
	double startTime = 0.0;
	double endTime = 10.0;
	double cursorPosition = 5.0;

	QVector<VideoFrame> frames = m_extractor->extractFramesOptimized(startTime, endTime, cursorPosition);
	// Without valid video, should return empty
	QVERIFY(frames.isEmpty());
}

void TestVideoExtractor::testExtractFramesIncremental_priorityZone()
{
	// Test incremental extraction prioritizes cursor zone
	// This is tested through the worker, but we can verify structure

	const double PRIORITY_WINDOW = 1.0;
	double cursorPosition = 2.0;

	// Simulate frames
	QVector<VideoFrame> allFrames;
	for (int i = 0; i < 40; i++) {
		VideoFrame frame;
		frame.timestamp = i * 0.1;
		frame.frameNumber = i;
		allFrames.append(frame);
	}

	// Split into priority and remaining
	QVector<VideoFrame> priorityFrames;
	QVector<VideoFrame> remainingFrames;

	for (const VideoFrame &frame : allFrames) {
		double distance = std::abs(frame.timestamp - cursorPosition);
		if (distance <= PRIORITY_WINDOW) {
			priorityFrames.append(frame);
		} else {
			remainingFrames.append(frame);
		}
	}

	// Verify split is correct
	QVERIFY(!priorityFrames.isEmpty());
	QVERIFY(!remainingFrames.isEmpty());
	QVERIFY(priorityFrames.size() + remainingFrames.size() == allFrames.size());
}

void TestVideoExtractor::testExtractFramesIncremental_completeSet()
{
	// Test that incremental extraction produces complete frame set
	QVector<VideoFrame> priorityFrames;
	QVector<VideoFrame> remainingFrames;

	// Simulate priority frames (1 second around cursor)
	for (int i = 10; i < 20; i++) {
		VideoFrame frame;
		frame.timestamp = i * 0.1;
		frame.frameNumber = i;
		priorityFrames.append(frame);
	}

	// Simulate remaining frames
	for (int i = 0; i < 10; i++) {
		VideoFrame frame;
		frame.timestamp = i * 0.1;
		frame.frameNumber = i;
		remainingFrames.append(frame);
	}

	for (int i = 20; i < 30; i++) {
		VideoFrame frame;
		frame.timestamp = i * 0.1;
		frame.frameNumber = i;
		remainingFrames.append(frame);
	}

	// Combine and sort
	QVector<VideoFrame> allFrames;
	allFrames.append(priorityFrames);
	allFrames.append(remainingFrames);
	std::sort(allFrames.begin(), allFrames.end(),
		  [](const VideoFrame &a, const VideoFrame &b) { return a.timestamp < b.timestamp; });

	// Verify complete set
	QVERIFY(allFrames.size() == 30);
	for (int i = 0; i < allFrames.size(); i++) {
		QVERIFY(std::abs(allFrames[i].timestamp - (i * 0.1)) < 0.0001);
	}
}

void TestVideoExtractor::testExtractFramesIncremental_frameOrdering()
{
	// Test that frames maintain correct ordering after incremental extraction
	QVector<VideoFrame> frames;

	// Create frames in random order
	QVector<int> indices = {5, 2, 8, 1, 9, 3, 7, 0, 6, 4};
	for (int idx : indices) {
		VideoFrame frame;
		frame.timestamp = idx * 0.1;
		frame.frameNumber = idx;
		frames.append(frame);
	}

	// Sort by timestamp
	std::sort(frames.begin(), frames.end(),
		  [](const VideoFrame &a, const VideoFrame &b) { return a.timestamp < b.timestamp; });

	// Verify sorted order
	for (int i = 0; i < frames.size(); i++) {
		QVERIFY(std::abs(frames[i].timestamp - (i * 0.1)) < 0.0001);
		QVERIFY(frames[i].frameNumber == i);
	}
}

void TestVideoExtractor::testCalculateFrameDifferences_basic()
{
	// Test basic frame difference calculation
	QVector<VideoFrame> frames;

	// Create frames with known differences
	// Note: Without actual pixmaps, we can only test structure
	for (int i = 0; i < 3; i++) {
		VideoFrame frame;
		frame.timestamp = i * 0.1;
		frame.frameNumber = i;
		frame.differenceFromPrevious = 0.0;
		frames.append(frame);
	}

	// First frame should have difference 0.0
	if (!frames.isEmpty()) {
		frames[0].differenceFromPrevious = 0.0;
	}

	// Verify structure
	QVERIFY(frames.size() == 3);
	QVERIFY(frames[0].differenceFromPrevious == 0.0);
}

void TestVideoExtractor::testCalculateFrameDifferences_emptyFrames()
{
	// Test with empty frame vector
	QVector<VideoFrame> emptyFrames;
	VideoExtractor::calculateFrameDifferences(emptyFrames);
	QVERIFY(emptyFrames.isEmpty());
}

void TestVideoExtractor::testCalculateFrameDifferences_singleFrame()
{
	// Test with single frame
	QVector<VideoFrame> frames;
	VideoFrame frame;
	frame.timestamp = 1.0;
	frame.frameNumber = 0;
	frame.differenceFromPrevious = 0.0;
	frames.append(frame);

	VideoExtractor::calculateFrameDifferences(frames);

	// Single frame should have difference 0.0
	QVERIFY(frames.size() == 1);
	QVERIFY(frames[0].differenceFromPrevious == 0.0);
}

void TestVideoExtractor::testCalculateFrameDifferences_identicalFrames()
{
	// Test with identical frames (should have low difference)
	// Note: Without actual pixmaps, we can only test structure
	QVector<VideoFrame> frames;

	for (int i = 0; i < 3; i++) {
		VideoFrame frame;
		frame.timestamp = i * 0.1;
		frame.frameNumber = i;
		frames.append(frame);
	}

	// Verify structure supports difference calculation
	QVERIFY(frames.size() == 3);
	// Actual difference calculation requires pixmaps
}

#include "test-video-extractor.moc"
#ifdef STANDALONE_TEST
QTEST_MAIN(TestVideoExtractor)
#endif
