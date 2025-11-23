/*
OBS Audio Sync Plugin - Tests - Header
Copyright (C) 2025 Ryan Wooden

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#ifndef TEST_AUDIO_ANALYZER_H
#define TEST_AUDIO_ANALYZER_H

#include <QtTest/QtTest>
#include <QCoreApplication>

class AudioAnalyzer;

class TestAudioAnalyzer : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void cleanupTestCase();
	void testGetFileDuration_invalidFile();
	void testAudioSampleStructure();

private:
	AudioAnalyzer *m_analyzer;
};

#endif // TEST_AUDIO_ANALYZER_H
