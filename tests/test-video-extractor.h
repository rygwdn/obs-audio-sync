/*
OBS Audio Sync Plugin - Tests - Header
Copyright (C) 2025 Ryan Wooden

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#ifndef TEST_VIDEO_EXTRACTOR_H
#define TEST_VIDEO_EXTRACTOR_H

#include <QtTest/QtTest>
#include <QCoreApplication>

class VideoExtractor;

class TestVideoExtractor : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void cleanupTestCase();
	void testOpenFile_invalidFile();
	void testOpenFile_emptyPath();
	void testVideoFrameStructure();
	void testExtractFrameAt_invalidFile();
	void testExtractFrames_invalidFile();
	void testGetFPS_defaultValue();
	void testGetDuration_defaultValue();
	void testClose();

	// Integration tests for optimized extraction
	void testDecodeFramesToNative_basic();
	void testDecodeFramesToNative_emptyRange();
	void testDecodeFramesToNative_invalidFile();
	void testDecodeFramesToNative_frameOrdering();
	void testDecodeFramesToNative_timestampAccuracy();

	void testConvertSingleNativeFrameToRGB_basic();
	void testConvertSingleNativeFrameToRGB_nullFrame();
	void testConvertSingleNativeFrameToRGB_threadSafety();

	void testConvertNativeFramesToRGB_basic();
	void testConvertNativeFramesToRGB_cursorPriority();
	void testConvertNativeFramesToRGB_parallelConversion();
	void testConvertNativeFramesToRGB_frameOrdering();
	void testConvertNativeFramesToRGB_emptyInput();

	void testExtractFramesOptimized_basic();
	void testExtractFramesOptimized_cursorPosition();
	void testExtractFramesOptimized_priorityZoneFirst();
	void testExtractFramesOptimized_frameDifferences();
	void testExtractFramesOptimized_edgeCases();
	void testExtractFramesOptimized_largeRange();

	void testExtractFramesIncremental_priorityZone();
	void testExtractFramesIncremental_completeSet();
	void testExtractFramesIncremental_frameOrdering();

	void testCalculateFrameDifferences_basic();
	void testCalculateFrameDifferences_emptyFrames();
	void testCalculateFrameDifferences_singleFrame();
	void testCalculateFrameDifferences_identicalFrames();

private:
	VideoExtractor *m_extractor;
	QString m_testVideoPath; // Path to a test video file (if available)
	bool m_hasTestVideo{false};

	// Helper methods (for future use with actual test video files)
	// bool createTestVideo(const QString &path, double duration, double fps);
	// void cleanupTestVideo(const QString &path);
};

#endif // TEST_VIDEO_EXTRACTOR_H
