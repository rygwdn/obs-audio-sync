/*
OBS Audio Sync Plugin - Tests (Standalone) - Header
Copyright (C) 2025 Ryan Wooden

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#ifndef TEST_RECORDING_SCANNER_STANDALONE_H
#define TEST_RECORDING_SCANNER_STANDALONE_H

#include <QtTest/QtTest>
#include <QCoreApplication>

class TestRecordingScannerStandalone : public QObject {
	Q_OBJECT

private slots:
	void testIsValidVideoFile();
};

#endif // TEST_RECORDING_SCANNER_STANDALONE_H
