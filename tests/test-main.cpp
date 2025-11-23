/*
OBS Audio Sync Plugin - Test Runner
Copyright (C) 2025 Ryan Wooden

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include <QtTest/QtTest>
#include <QApplication>
#include <QCoreApplication>

// Include test class headers
#include "test-recording-scanner-standalone.h"
#include "test-timeline-widget.h"
#include "test-audio-analyzer.h"

int main(int argc, char *argv[])
{
	// Use QApplication since TestTimelineWidget requires it
	QApplication app(argc, argv);

	int result = 0;

	// Run TestRecordingScannerStandalone
	{
		TestRecordingScannerStandalone test;
		if (QTest::qExec(&test, argc, argv) != 0) {
			result = 1;
		}
	}
	
	// Run TestTimelineWidget
	{
		TestTimelineWidget test;
		if (QTest::qExec(&test, argc, argv) != 0) {
			result = 1;
		}
	}
	
	// Run TestAudioAnalyzer
	{
		TestAudioAnalyzer test;
		if (QTest::qExec(&test, argc, argv) != 0) {
			result = 1;
		}
	}
	
	return result;
}
