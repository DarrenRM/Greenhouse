#include "../include/BlackoutController.h"

#include <QCursor>
#include <QDebug>
#include <QEvent>
#include <QGuiApplication>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QWidget>

#include <windows.h>

#include <bitset>

namespace {

// Input during this window is ignored so releasing the button (or the hand
// still resting on the mouse) does not immediately cancel the blackout.
constexpr int kArmDelayMs = 1000;
constexpr int kInputPollMs = 50;
// A hand resting on a high-DPI mouse produces constant sub-pixel and
// single-pixel jitter; only deliberate movement past this radius wakes.
constexpr int kMouseDeadZonePx = 12;

class BlackoutOverlay final : public QWidget {
public:
    explicit BlackoutOverlay(QScreen* screen)
        : QWidget(nullptr,
                  Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                      Qt::Tool | Qt::NoDropShadowWindowHint)
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setAttribute(Qt::WA_OpaquePaintEvent);
        setAttribute(Qt::WA_NoSystemBackground);
        setCursor(Qt::BlankCursor);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        setScreen(screen);
        setGeometry(screen->geometry());
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter(this).fillRect(rect(), Qt::black);
    }
};

QPoint physicalCursorPos()
{
    POINT point = {};
    GetCursorPos(&point);
    return QPoint(point.x, point.y);
}

std::bitset<256> keysAndButtonsDown()
{
    std::bitset<256> down;
    for (int vk = 1; vk < 256; ++vk) {
        if (GetAsyncKeyState(vk) & 0x8000) {
            down.set(vk);
        }
    }
    return down;
}

} // namespace

BlackoutController::BlackoutController(QObject* parent)
    : QObject(parent),
      m_dimmedLevels(std::make_shared<QList<MonitorBrightness::Level>>())
{
    m_brightnessPool.setMaxThreadCount(1);

    m_inputPollTimer = new QTimer(this);
    m_inputPollTimer->setInterval(kInputPollMs);
    connect(m_inputPollTimer, &QTimer::timeout, this, &BlackoutController::pollInput);

    m_armTimer = new QTimer(this);
    m_armTimer->setSingleShot(true);
    m_armTimer->setInterval(kArmDelayMs);
    connect(m_armTimer, &QTimer::timeout, this, [this]() {
        m_armedCursorPos = physicalCursorPos();
        m_keysDownAtArm = keysAndButtonsDown();
        m_armed = true;
    });

    // Undo a dim left behind by a previous run that exited mid-blackout.
    m_brightnessPool.start([]() {
        const QList<MonitorBrightness::Level> pending = MonitorBrightness::takePending();
        if (!pending.isEmpty()) {
            qInfo() << "Blackout: restoring brightness left dimmed by a previous run.";
            MonitorBrightness::restore(pending);
        }
    });
}

BlackoutController::~BlackoutController()
{
    stop();
    m_brightnessPool.waitForDone();
}

void BlackoutController::start()
{
    if (m_active) {
        return;
    }
    qInfo() << "Blackout: starting.";
    m_active = true;
    m_armed = false;

    createOverlays();
    m_armTimer->start();
    m_inputPollTimer->start();

    auto levels = m_dimmedLevels;
    m_brightnessPool.start([levels]() {
        *levels = MonitorBrightness::dimAll();
        MonitorBrightness::persistPending(*levels);
    });

    emit activeChanged(true);
}

void BlackoutController::stop()
{
    if (!m_active) {
        return;
    }
    qInfo() << "Blackout: stopping.";
    m_active = false;
    m_armed = false;
    m_armTimer->stop();
    m_inputPollTimer->stop();
    destroyOverlays();

    // Queued behind any in-flight dim, so it always sees the final levels.
    auto levels = m_dimmedLevels;
    m_brightnessPool.start([levels]() {
        MonitorBrightness::restore(*levels);
        levels->clear();
        MonitorBrightness::persistPending({});
    });

    emit activeChanged(false);
}

void BlackoutController::pollInput()
{
    if (!m_armed) {
        return;
    }

    const QPoint moved = physicalCursorPos() - m_armedCursorPos;
    if (moved.manhattanLength() > kMouseDeadZonePx) {
        qInfo() << "Blackout: mouse moved" << moved;
        stop();
        return;
    }

    // Backup for keys and buttons in case another window took focus away
    // from the overlay; keys already held when arming are ignored.
    const std::bitset<256> newlyDown = keysAndButtonsDown() & ~m_keysDownAtArm;
    if (newlyDown.any()) {
        qInfo() << "Blackout: key or button pressed.";
        stop();
    }
}

bool BlackoutController::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
    case QEvent::Wheel:
        // Swallow the input that wakes the screen so it doesn't reach any app.
        if (m_armed) {
            qInfo() << "Blackout: overlay input detected.";
            QTimer::singleShot(0, this, &BlackoutController::stop);
        }
        return true;
    case QEvent::KeyRelease:
    case QEvent::MouseButtonRelease:
        return true;
    default:
        return QObject::eventFilter(watched, event);
    }
}

void BlackoutController::createOverlays()
{
    QWidget* focusOverlay = nullptr;
    QScreen* cursorScreen = QGuiApplication::screenAt(QCursor::pos());
    for (QScreen* screen : QGuiApplication::screens()) {
        auto* overlay = new BlackoutOverlay(screen);
        overlay->installEventFilter(this);
        overlay->show();
        m_overlays.append(overlay);
        if (screen == cursorScreen || !focusOverlay) {
            focusOverlay = overlay;
        }
    }

    // Holding keyboard focus keeps the waking key press out of other apps.
    if (focusOverlay) {
        focusOverlay->raise();
        focusOverlay->activateWindow();
        focusOverlay->setFocus();
    }
}

void BlackoutController::destroyOverlays()
{
    for (const QPointer<QWidget>& overlay : m_overlays) {
        if (overlay) {
            overlay->close();
        }
    }
    m_overlays.clear();
}
