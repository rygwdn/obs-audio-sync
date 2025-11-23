/*
OBS Audio Sync Plugin - Tests - Header
Copyright (C) 2025 Ryan Wooden

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#ifndef TEST_TIMELINE_WIDGET_H
#define TEST_TIMELINE_WIDGET_H

#include <QtTest/QtTest>
#include <QApplication>

class TimelineWidget;

class TestTimelineWidget : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void cleanupTestCase();
	void testSetAudioSamples();
	void testSetSpikePosition();
	void testSetFPS();
	void testTimestampConversion();

private:
	TimelineWidget *m_widget;
};

#endif // TEST_TIMELINE_WIDGET_H
