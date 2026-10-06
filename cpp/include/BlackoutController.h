#pragma once

#include <QList>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QThreadPool>

#include <bitset>
#include <memory>

#include "MonitorBrightness.h"

class QTimer;
class QWidget;

// Screensaver-style blackout: covers every screen with a black, cursorless
// topmost window and drops monitor backlights over DDC/CI. Any mouse
// movement, click, wheel or key press after a short grace period ends it.
class BlackoutController : public QObject {
    Q_OBJECT

public:
    explicit BlackoutController(QObject* parent = nullptr);
    ~BlackoutController() override;

    bool isActive() const { return m_active; }

public slots:
    void start();
    void stop();

signals:
    void activeChanged(bool active);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void pollInput();
    void createOverlays();
    void destroyOverlays();

    bool m_active = false;
    bool m_armed = false;
    QPoint m_armedCursorPos;
    std::bitset<256> m_keysDownAtArm;
    QList<QPointer<QWidget>> m_overlays;
    QTimer* m_inputPollTimer = nullptr;
    QTimer* m_armTimer = nullptr;

    // Single-threaded pool so dim and restore never overlap or reorder.
    QThreadPool m_brightnessPool;
    std::shared_ptr<QList<MonitorBrightness::Level>> m_dimmedLevels;
};
