#pragma once

#include <QList>
#include <QString>

// Hardware backlight control for external monitors over DDC/CI (dxva2).
// Every call talks to the monitor over I2C and can take hundreds of
// milliseconds, so callers must keep these off the UI thread.
namespace MonitorBrightness {

struct Level {
    QString monitorDevice;   // e.g. \\.\DISPLAY1
    int physicalIndex = 0;   // index within that HMONITOR's physical monitors
    int original = 0;        // brightness to restore
};

// Drops every DDC/CI-capable monitor to its minimum brightness and returns
// the levels needed to undo it. Monitors without DDC/CI are skipped.
QList<Level> dimAll();

// Puts each monitor back to its recorded brightness.
void restore(const QList<Level>& levels);

// Crash safety: the dimmed levels are persisted until restored so a killed
// process does not leave the monitors at minimum brightness.
void persistPending(const QList<Level>& levels);
QList<Level> takePending();

} // namespace MonitorBrightness
