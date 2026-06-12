#include "GamepadController.h"
#include "player/MpvController.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QDebug>
#include <QWindow>
#include <QTimer>

// ── Binding / Action tables ──────────────────────────────────────────────────

struct BindingDef { const char *id; const char *label; };
struct ActionDef  { const char *id; const char *label; int qtKey; };

static const BindingDef kBindings[] = {
    { "none",       "None" },
    { "hat_up",     "D-Pad Up" },
    { "hat_down",   "D-Pad Down" },
    { "hat_left",   "D-Pad Left" },
    { "hat_right",  "D-Pad Right" },
    { "btn_0",      "Btn 0 (A)" },
    { "btn_1",      "Btn 1 (B)" },
    { "btn_2",      "Btn 2 (X)" },
    { "btn_3",      "Btn 3 (Y)" },
    { "btn_4",      "Btn 4 (LB)" },
    { "btn_5",      "Btn 5 (RB)" },
    { "btn_6",      "Btn 6 (Back)" },
    { "btn_7",      "Btn 7 (Start)" },
    { "btn_8",      "Btn 8" },
    { "btn_9",      "Btn 9" },
    { "btn_10",     "Btn 10" },
    { "btn_11",     "Btn 11" },
};
static const int kNumBindings = int(sizeof(kBindings) / sizeof(kBindings[0]));

static const ActionDef kActions[] = {
    { "up",     "Navigate Up",    Qt::Key_Up },
    { "down",   "Navigate Down",  Qt::Key_Down },
    { "left",   "Navigate Left",  Qt::Key_Left },
    { "right",  "Navigate Right", Qt::Key_Right },
    { "select", "Select",         Qt::Key_Return },
    { "back",   "Back",           Qt::Key_Escape },
};
static const int kNumActions = int(sizeof(kActions) / sizeof(kActions[0]));

// Axis index + direction → binding ID.
// Only hat axes (6/7) are listed here. Stick axes (0/1, 3/4) are remapped to
// the equivalent hat axis in onAxisEvent() based on stickMode — so stick input
// transparently drives the same hat_* bindings without requiring the user to
// reconfigure every action.
struct AxisMap { int axis; int dir; const char *bindingId; };
static const AxisMap kAxisMaps[] = {
    { 6, -1, "hat_left" }, { 6, +1, "hat_right" },
    { 7, -1, "hat_up" },   { 7, +1, "hat_down" },
};
static const int kNumAxisMaps = int(sizeof(kAxisMaps) / sizeof(kAxisMaps[0]));

// 50% deflection threshold — avoids drift false-positives on analog sticks.
static const int kAxisThreshold = 16384;

// Qt key → mpv IPC key name, for forwarding input while mpv is playing.
struct MpvKeyMap { int qtKey; const char *mpvKey; };
static const MpvKeyMap kMpvKeyMaps[] = {
    { Qt::Key_Up,     "UP" },
    { Qt::Key_Down,   "DOWN" },
    { Qt::Key_Left,   "LEFT" },
    { Qt::Key_Right,  "RIGHT" },
    { Qt::Key_Return, "ENTER" },
    { Qt::Key_Escape, "ESC" },
};
static const int kNumMpvKeyMaps = int(sizeof(kMpvKeyMaps) / sizeof(kMpvKeyMaps[0]));

static const QMap<QString, QString> kDefaults = {
    { "up",     "hat_up" },
    { "down",   "hat_down" },
    { "left",   "hat_left" },
    { "right",  "hat_right" },
    { "select", "btn_0" },
    { "back",   "btn_1" },
};
static const QString kDefaultStickMode = QStringLiteral("ls");

// ── Common implementation ────────────────────────────────────────────────────

GamepadController::GamepadController(const QString &dataRoot, MpvController *mpv, QObject *parent)
    : QObject(parent), m_dataRoot(dataRoot), m_mpv(mpv)
{
    loadMappings();

    // Auto-repeat: initial delay then continuous repeat, matching keyboard behaviour.
    m_repeatDelayTimer = new QTimer(this);
    m_repeatDelayTimer->setSingleShot(true);
    m_repeatTimer = new QTimer(this);

    connect(m_repeatDelayTimer, &QTimer::timeout, this, [this] {
        dispatchPress(m_repeatKey);
        m_repeatTimer->start(80);
    });
    connect(m_repeatTimer, &QTimer::timeout, this, [this] {
        dispatchPress(m_repeatKey);
    });

    platformInit();
}

GamepadController::~GamepadController()
{
    platformShutdown();
}

void GamepadController::setConnected(bool c)
{
    if (m_connected == c) return;
    m_connected = c;
    emit connectedChanged();
    qDebug("[GamepadController] %s", c ? "connected" : "disconnected");
}

void GamepadController::loadMappings()
{
    QFile f(m_dataRoot + "/config.json");
    QJsonObject gamepad;
    if (f.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isObject())
            gamepad = doc.object()["app"].toObject()["gamepad"].toObject();
    }
    m_mappings.clear();
    for (int i = 0; i < kNumActions; ++i) {
        QString id = QString::fromLatin1(kActions[i].id);
        m_mappings[id] = gamepad.contains(id) ? gamepad[id].toString()
                                               : kDefaults.value(id, QStringLiteral("none"));
    }
    m_stickMode = gamepad.contains("stick_mode") ? gamepad["stick_mode"].toString()
                                                  : kDefaultStickMode;
}

void GamepadController::saveMappings()
{
    QFile f(m_dataRoot + "/config.json");
    QJsonObject root;
    if (f.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isObject()) root = doc.object();
        f.close();
    }
    QJsonObject app = root["app"].toObject();
    QJsonObject gamepad;
    for (auto it = m_mappings.constBegin(); it != m_mappings.constEnd(); ++it)
        gamepad[it.key()] = it.value();
    gamepad["stick_mode"] = m_stickMode;
    app["gamepad"] = gamepad;
    root["app"] = app;
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

// Sends a single press without touching the repeat timers (called by the timers).
void GamepadController::dispatchPress(int qtKey)
{
    if (m_mpv && m_mpv->isPlaying()) {
        for (int i = 0; i < kNumMpvKeyMaps; ++i) {
            if (kMpvKeyMaps[i].qtKey == qtKey) {
                m_mpv->sendKey(QString::fromLatin1(kMpvKeyMaps[i].mpvKey));
                return;
            }
        }
        return;
    }
    QWindow *window = QGuiApplication::focusWindow();
    if (!window) return;
    QKeyEvent ev(QEvent::KeyPress, qtKey, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &ev);
}

void GamepadController::fireKey(int qtKey, bool pressed)
{
    if (pressed) {
        // Cancel any in-flight repeat from a different key.
        m_repeatDelayTimer->stop();
        m_repeatTimer->stop();
        m_repeatKey = qtKey;

        dispatchPress(qtKey);
        if (qtKey == Qt::Key_Up || qtKey == Qt::Key_Down ||
            qtKey == Qt::Key_Left || qtKey == Qt::Key_Right)
            m_repeatDelayTimer->start(400);
    } else {
        if (m_repeatKey == qtKey) {
            m_repeatKey = 0;
            m_repeatDelayTimer->stop();
            m_repeatTimer->stop();
        }
        // Send key-release for menu navigation (mpv ignores releases).
        if (!(m_mpv && m_mpv->isPlaying())) {
            QWindow *window = QGuiApplication::focusWindow();
            if (window) {
                QKeyEvent ev(QEvent::KeyRelease, qtKey, Qt::NoModifier);
                QCoreApplication::sendEvent(window, &ev);
            }
        }
    }
}

int GamepadController::actionToQtKey(const QString &actionId) const
{
    for (int i = 0; i < kNumActions; ++i) {
        if (actionId == QLatin1String(kActions[i].id))
            return kActions[i].qtKey;
    }
    return Qt::Key_unknown;
}

void GamepadController::checkAxisBinding(const QString &bindingId, bool active)
{
    for (auto it = m_mappings.constBegin(); it != m_mappings.constEnd(); ++it) {
        if (it.value() == bindingId) {
            int qtKey = actionToQtKey(it.key());
            if (qtKey != Qt::Key_unknown)
                fireKey(qtKey, active);
        }
    }
}

void GamepadController::onButtonEvent(int buttonIndex, bool pressed)
{
    QString bindingId = QStringLiteral("btn_%1").arg(buttonIndex);
    for (auto it = m_mappings.constBegin(); it != m_mappings.constEnd(); ++it) {
        if (it.value() == bindingId) {
            int qtKey = actionToQtKey(it.key());
            if (qtKey != Qt::Key_unknown)
                fireKey(qtKey, pressed);
        }
    }
}

void GamepadController::onAxisEvent(int axisIndex, int value)
{
    // Stick axes are remapped to their hat equivalents so stickMode transparently
    // enables stick-based navigation without needing per-action rebinding.
    //   Left stick:  X(0)→hat-X(6), Y(1)→hat-Y(7)
    //   Right stick: X(3)→hat-X(6), Y(4)→hat-Y(7)
    // The original axisIndex is kept for edge-detection keys so hat and stick
    // events don't share state and can both be active simultaneously.
    int lookupAxis = axisIndex;
    const bool isLs = (axisIndex == 0 || axisIndex == 1);
    const bool isRs = (axisIndex == 3 || axisIndex == 4);
    if (isLs) {
        if (m_stickMode != QLatin1String("ls") && m_stickMode != QLatin1String("both")) return;
        lookupAxis = (axisIndex == 0) ? 6 : 7;
    } else if (isRs) {
        if (m_stickMode != QLatin1String("rs") && m_stickMode != QLatin1String("both")) return;
        lookupAxis = (axisIndex == 3) ? 6 : 7;
    }

    for (int i = 0; i < kNumAxisMaps; ++i) {
        const AxisMap &am = kAxisMaps[i];
        if (am.axis != lookupAxis) continue;
        bool active = (am.dir < 0) ? (value < -kAxisThreshold) : (value > kAxisThreshold);
        // Use original axisIndex in the edge-detection key so stick and hat
        // track independently and don't interfere with each other.
        QString key = QStringLiteral("%1_%2").arg(axisIndex).arg(am.dir < 0 ? 'n' : 'p');
        bool wasActive = m_axisActive.value(key, false);
        if (active != wasActive) {
            m_axisActive[key] = active;
            checkAxisBinding(QString::fromLatin1(am.bindingId), active);
        }
    }
}

// ── Q_INVOKABLE API (called from QML settings UI) ───────────────────────────

QVariantList GamepadController::getActions() const
{
    QVariantList result;
    for (int i = 0; i < kNumActions; ++i) {
        QString id      = QString::fromLatin1(kActions[i].id);
        QString binding = m_mappings.value(id, kDefaults.value(id, QStringLiteral("none")));
        result.append(QVariantMap{
            { "id",           id },
            { "label",        QString::fromLatin1(kActions[i].label) },
            { "binding",      binding },
            { "bindingLabel", bindingLabel(binding) },
        });
    }
    return result;
}

QStringList GamepadController::availableBindingIds() const
{
    QStringList ids;
    ids.reserve(kNumBindings);
    for (int i = 0; i < kNumBindings; ++i)
        ids.append(QString::fromLatin1(kBindings[i].id));
    return ids;
}

QString GamepadController::bindingLabel(const QString &id) const
{
    for (int i = 0; i < kNumBindings; ++i) {
        if (id == QLatin1String(kBindings[i].id))
            return QString::fromLatin1(kBindings[i].label);
    }
    return id;
}

void GamepadController::setBinding(const QString &actionId, const QString &bindingId)
{
    if (m_mappings.value(actionId) == bindingId) return;
    m_mappings[actionId] = bindingId;
    saveMappings();
    qDebug("[GamepadController] %s → %s", qPrintable(actionId), qPrintable(bindingId));
}

void GamepadController::setStickMode(const QString &mode)
{
    if (m_stickMode == mode) return;
    m_stickMode = mode;
    m_axisActive.clear(); // reset edge-detection state so no phantom releases fire
    saveMappings();
    qDebug("[GamepadController] stickMode = %s", qPrintable(mode));
}

// ── Linux platform implementation ───────────────────────────────────────────

#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <linux/joystick.h>
#include <QSocketNotifier>
#include <QTimer>

struct GamepadController::PlatformData {
    int              fd             = -1;
    QSocketNotifier *notifier       = nullptr;
    QTimer          *reconnectTimer = nullptr;
};

void GamepadController::platformInit()
{
    m_pd = new PlatformData;

    m_pd->reconnectTimer = new QTimer(this);
    m_pd->reconnectTimer->setInterval(3000);
    connect(m_pd->reconnectTimer, &QTimer::timeout, this, [this]() {
        if (!m_connected) tryOpenJoystick();
    });
    m_pd->reconnectTimer->start();

    tryOpenJoystick();
}

void GamepadController::platformShutdown()
{
    if (!m_pd) return;
    if (m_pd->notifier) {
        m_pd->notifier->setEnabled(false);
        m_pd->notifier->deleteLater();
    }
    if (m_pd->fd >= 0) ::close(m_pd->fd);
    delete m_pd;
    m_pd = nullptr;
}

void GamepadController::tryOpenJoystick()
{
    for (int i = 0; i < 4; ++i) {
        QByteArray path = QStringLiteral("/dev/input/js%1").arg(i).toLocal8Bit();
        int fd = ::open(path.constData(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        m_pd->fd = fd;
        m_pd->notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);

        connect(m_pd->notifier, &QSocketNotifier::activated, this,
                [this](QSocketDescriptor, QSocketNotifier::Type) {
            struct js_event ev;
            ssize_t n;
            while ((n = ::read(m_pd->fd, &ev, sizeof(ev))) == (ssize_t)sizeof(ev)) {
                uint8_t type = ev.type & ~JS_EVENT_INIT;
                if (type == JS_EVENT_BUTTON)
                    onButtonEvent(ev.number, ev.value != 0);
                else if (type == JS_EVENT_AXIS)
                    onAxisEvent(ev.number, ev.value);
            }
            // n == 0: EOF; errno != EAGAIN/EINTR: real error (e.g. ENODEV on unplug)
            if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) {
                qDebug("[GamepadController] Joystick disconnected (errno=%d)", errno);
                m_pd->notifier->setEnabled(false);
                m_pd->notifier->deleteLater();
                m_pd->notifier = nullptr;
                ::close(m_pd->fd);
                m_pd->fd = -1;
                setConnected(false);
            }
        });

        setConnected(true);
        qDebug("[GamepadController] Opened /dev/input/js%d", i);
        return;
    }
}

#endif // Q_OS_LINUX
