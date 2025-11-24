/*
OBS Audio Sync Plugin - Tests (Standalone)
Copyright (C) 2025 Ryan Wooden

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "test-recording-scanner-standalone.h"
#include "../src/recording-scanner.h"

void TestRecordingScannerStandalone::testIsValidVideoFile()
{
	RecordingScanner scanner;

	// Test valid extensions
	QVERIFY(scanner.isValidVideoFile("test.mp4"));
	QVERIFY(scanner.isValidVideoFile("test.MKV"));
	QVERIFY(scanner.isValidVideoFile("test.flv"));
	QVERIFY(scanner.isValidVideoFile("test.mov"));
	QVERIFY(scanner.isValidVideoFile("test.avi"));
	QVERIFY(scanner.isValidVideoFile("test.webm"));

	// Test invalid extensions
	QVERIFY(!scanner.isValidVideoFile("test.txt"));
	QVERIFY(!scanner.isValidVideoFile("test.jpg"));
	QVERIFY(!scanner.isValidVideoFile("test"));
	QVERIFY(!scanner.isValidVideoFile(""));
}

#include "test-recording-scanner-standalone.moc"
#ifdef STANDALONE_TEST
QTEST_MAIN(TestRecordingScannerStandalone)
#endif
