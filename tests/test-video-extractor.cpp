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

#include "test-video-extractor.moc"
#ifdef STANDALONE_TEST
QTEST_MAIN(TestVideoExtractor)
#endif
