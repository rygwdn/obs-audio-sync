/*
OBS Audio Sync Plugin - Tests
Copyright (C) 2025 Ryan Wooden

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include <QtTest/QtTest>
#include <QCoreApplication>
#include "../src/audio-analyzer.h"
#include <QFileInfo>

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

void TestAudioAnalyzer::initTestCase()
{
	m_analyzer = new AudioAnalyzer();
}

void TestAudioAnalyzer::cleanupTestCase()
{
	delete m_analyzer;
}

void TestAudioAnalyzer::testGetFileDuration_invalidFile()
{
	// Test with non-existent file
	double duration = m_analyzer->getFileDuration("/nonexistent/file.mp4");
	QVERIFY(duration == 0.0);
}

void TestAudioAnalyzer::testAudioSampleStructure()
{
	// Test AudioSample structure
	AudioSample sample;
	sample.timestamp = 1.5;
	sample.amplitude = 0.75;
	
	QVERIFY(sample.timestamp == 1.5);
	QVERIFY(sample.amplitude == 0.75);
	
	// Test AudioSpike structure
	AudioSpike spike;
	spike.timestamp = 2.0;
	spike.amplitude = 1.0;
	spike.windowStart = 0.0;
	spike.windowEnd = 4.0;
	
	QVERIFY(spike.timestamp == 2.0);
	QVERIFY(spike.amplitude == 1.0);
	QVERIFY(spike.windowStart == 0.0);
	QVERIFY(spike.windowEnd == 4.0);
}

int main(int argc, char *argv[])
{
	QCoreApplication app(argc, argv);
	TestAudioAnalyzer test;
	return QTest::qExec(&test, argc, argv);
}

#include "test-audio-analyzer.moc"
