#include "../include/MonitorBrightness.h"

#include <QDebug>
#include <QSettings>
#include <QVariantList>
#include <QVariantMap>

#include <windows.h>
#include <highlevelmonitorconfigurationapi.h>
#include <physicalmonitorenumerationapi.h>

#include <functional>
#include <vector>

namespace {

const QString kPendingKey = QStringLiteral("blackout/pendingBrightness");

using PhysicalMonitorVisitor =
    std::function<void(const QString& device, int index, HANDLE monitor)>;

BOOL CALLBACK collectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data)
{
    reinterpret_cast<std::vector<HMONITOR>*>(data)->push_back(monitor);
    return TRUE;
}

void forEachPhysicalMonitor(const PhysicalMonitorVisitor& visit)
{
    std::vector<HMONITOR> monitors;
    EnumDisplayMonitors(nullptr, nullptr, collectMonitor, reinterpret_cast<LPARAM>(&monitors));

    for (HMONITOR monitor : monitors) {
        MONITORINFOEXW info = {};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(monitor, &info)) {
            continue;
        }
        const QString device = QString::fromWCharArray(info.szDevice);

        DWORD count = 0;
        if (!GetNumberOfPhysicalMonitorsFromHMONITOR(monitor, &count) || count == 0) {
            continue;
        }
        std::vector<PHYSICAL_MONITOR> physical(count);
        if (!GetPhysicalMonitorsFromHMONITOR(monitor, count, physical.data())) {
            continue;
        }
        for (DWORD index = 0; index < count; ++index) {
            visit(device, static_cast<int>(index), physical[index].hPhysicalMonitor);
        }
        DestroyPhysicalMonitors(count, physical.data());
    }
}

} // namespace

namespace MonitorBrightness {

QList<Level> dimAll()
{
    QList<Level> levels;
    forEachPhysicalMonitor([&levels](const QString& device, int index, HANDLE monitor) {
        DWORD minimum = 0;
        DWORD current = 0;
        DWORD maximum = 0;
        if (!GetMonitorBrightness(monitor, &minimum, &current, &maximum)) {
            qInfo() << "Blackout: no DDC/CI brightness control for" << device << index;
            return;
        }
        if (current <= minimum) {
            return;
        }
        if (!SetMonitorBrightness(monitor, minimum)) {
            qWarning() << "Blackout: failed to dim" << device << index
                       << "error" << GetLastError();
            return;
        }
        levels.append(Level{device, index, static_cast<int>(current)});
        qInfo() << "Blackout: dimmed" << device << index << "from" << current << "to" << minimum;
    });
    return levels;
}

void restore(const QList<Level>& levels)
{
    if (levels.isEmpty()) {
        return;
    }
    forEachPhysicalMonitor([&levels](const QString& device, int index, HANDLE monitor) {
        for (const Level& level : levels) {
            if (level.physicalIndex != index ||
                level.monitorDevice.compare(device, Qt::CaseInsensitive) != 0) {
                continue;
            }
            if (SetMonitorBrightness(monitor, static_cast<DWORD>(level.original))) {
                qInfo() << "Blackout: restored" << device << index << "to" << level.original;
            } else {
                qWarning() << "Blackout: failed to restore" << device << index
                           << "error" << GetLastError();
            }
        }
    });
}

void persistPending(const QList<Level>& levels)
{
    QSettings settings;
    if (levels.isEmpty()) {
        settings.remove(kPendingKey);
        return;
    }
    QVariantList stored;
    for (const Level& level : levels) {
        stored.append(QVariantMap{
            {QStringLiteral("device"), level.monitorDevice},
            {QStringLiteral("index"), level.physicalIndex},
            {QStringLiteral("original"), level.original},
        });
    }
    settings.setValue(kPendingKey, stored);
    settings.sync();
}

QList<Level> takePending()
{
    QSettings settings;
    QList<Level> levels;
    for (const QVariant& value : settings.value(kPendingKey).toList()) {
        const QVariantMap map = value.toMap();
        levels.append(Level{map.value(QStringLiteral("device")).toString(),
                            map.value(QStringLiteral("index")).toInt(),
                            map.value(QStringLiteral("original")).toInt()});
    }
    settings.remove(kPendingKey);
    settings.sync();
    return levels;
}

} // namespace MonitorBrightness
