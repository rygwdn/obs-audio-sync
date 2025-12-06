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

private:
	VideoExtractor *m_extractor;
};

#endif // TEST_VIDEO_EXTRACTOR_H
