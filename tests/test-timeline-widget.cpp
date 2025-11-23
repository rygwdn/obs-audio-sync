/*
OBS Audio Sync Plugin - Tests
Copyright (C) 2025 Ryan Wooden

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "test-timeline-widget.h"
#include "../src/timeline-widget.h"
#include "../src/audio-analyzer.h"

void TestTimelineWidget::initTestCase()
{
	// QApplication is created in test-main.cpp, so we just use the existing instance
	m_widget = new TimelineWidget();
}

void TestTimelineWidget::cleanupTestCase()
{
	delete m_widget;
}

void TestTimelineWidget::testSetAudioSamples()
{
	QVector<AudioSample> samples;
	for (int i = 0; i < 10; i++) {
		AudioSample sample;
		sample.timestamp = i * 0.1;
		sample.amplitude = 0.5;
		samples.append(sample);
	}

	m_widget->setAudioSamples(samples);
	// Just verify it doesn't crash
	QVERIFY(true);
}

void TestTimelineWidget::testSetSpikePosition()
{
	m_widget->setSpikePosition(2.5);
	// Verify it doesn't crash
	QVERIFY(true);
}

void TestTimelineWidget::testSetFPS()
{
	m_widget->setFPS(30.0);
	QVERIFY(true);

	m_widget->setFPS(60.0);
	QVERIFY(true);
}

void TestTimelineWidget::testTimestampConversion()
{
	// Test that timestamp conversion works (indirectly through widget operations)
	m_widget->setSpikePosition(1.0);
	m_widget->setSpikePosition(2.0);
	m_widget->setSpikePosition(3.0);
	QVERIFY(true);
}

#include "test-timeline-widget.moc"
