#pragma once
#include <QObject>
#include <QMap>
#include <QVariantList>
#include <QTimer>

class MpvController;

// GamepadController translates gamepad input into synthetic Qt key events so the
// existing keyboard-driven QML navigation works unchanged. Button → action bindings
// are stored in config.json under "app.gamepad" and are configurable from the UI.
//
// Platform implementations:
//   Linux:  /dev/input/js* (joystick API) via QSocketNotifier — no extra deps
//   macOS:  GameController.framework via GamepadController_macos.mm

class GamepadController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
public:
    explicit GamepadController(const QString &dataRoot, MpvController *mpv = nullptr, QObject *parent = nullptr);
    ~GamepadController();

    bool isConnected() const { return m_connected; }

    // Called by the Gamepad Settings UI
    Q_INVOKABLE QVariantList getActions() const;             // [{id, label, binding, bindingLabel}]
    Q_INVOKABLE QStringList  availableBindingIds() const;    // ordered list of all bindingIDs
    Q_INVOKABLE QString      bindingLabel(const QString &id) const;
    Q_INVOKABLE void         setBinding(const QString &actionId, const QString &bindingId);
    Q_INVOKABLE QString      stickMode() const { return m_stickMode; }
    Q_INVOKABLE void         setStickMode(const QString &mode);

    // Public so the macOS .mm platform layer can call them from ObjC blocks
    void onButtonEvent(int buttonIndex, bool pressed);
    void onAxisEvent(int axisIndex, int value);  // value: -32767..+32767
    void setConnected(bool c);

signals:
    void connectedChanged();

private:
    void loadMappings();
    void saveMappings();
    void dispatchPress(int qtKey);
    void fireKey(int qtKey, bool pressed);
    int  actionToQtKey(const QString &actionId) const;
    void checkAxisBinding(const QString &bindingId, bool active);

    void platformInit();
    void platformShutdown();

#ifdef Q_OS_LINUX
    void tryOpenJoystick();
#endif

    QString                m_dataRoot;
    MpvController         *m_mpv        = nullptr;
    bool                   m_connected  = false;
    QString                m_stickMode;             // "ls" | "rs" | "both" | "none"
    QMap<QString, QString> m_mappings;              // actionId → bindingId
    QMap<QString, bool>    m_axisActive;            // "axisIdx_dir" → wasActive (edge detection)

    // Auto-repeat state
    QTimer                *m_repeatDelayTimer = nullptr;
    QTimer                *m_repeatTimer      = nullptr;
    int                    m_repeatKey        = 0;

    struct PlatformData;
    PlatformData *m_pd = nullptr;
};
