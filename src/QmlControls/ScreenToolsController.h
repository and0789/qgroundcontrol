#pragma once

#include <QtCore/QEvent>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtGui/QInputDevice>
#include <QtQmlIntegration/QtQmlIntegration>

/// \brief This Qml control is used to return screen parameters
///
class ScreenToolsController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool isAndroid READ isAndroid CONSTANT)
    Q_PROPERTY(bool isiOS READ isiOS CONSTANT)
    Q_PROPERTY(bool isMobile READ isMobile CONSTANT)
    Q_PROPERTY(bool fakeMobile READ fakeMobile CONSTANT)
    Q_PROPERTY(bool isDebug READ isDebug CONSTANT)
    Q_PROPERTY(bool isMacOS READ isMacOS CONSTANT)
    Q_PROPERTY(bool isLinux READ isLinux CONSTANT)
    Q_PROPERTY(bool isWindows READ isWindows CONSTANT)
    Q_PROPERTY(bool isSerialAvailable READ isSerialAvailable CONSTANT)
    Q_PROPERTY(bool hasTouch READ hasTouch CONSTANT)
    Q_PROPERTY(QString iOSDevice READ iOSDevice CONSTANT)
    Q_PROPERTY(QString fixedFontFamily READ fixedFontFamily CONSTANT)
    Q_PROPERTY(QString normalFontFamily READ normalFontFamily CONSTANT)
    Q_PROPERTY(QString probeLog READ probeLog NOTIFY probeLogChanged)

public:
    explicit ScreenToolsController(QObject* parent = nullptr);
    ~ScreenToolsController();

    /// DIAGNOSTIC ONLY. Starts recording every pointer event the application receives, so a device
    /// whose taps go nowhere can be read off the screen rather than guessed at.
    Q_INVOKABLE void startPointerProbe();

    QString probeLog() const { return _probeLog; }

    /// Returns current mouse position
    Q_INVOKABLE static int mouseX();
    Q_INVOKABLE static int mouseY();

    // QFontMetrics::descent for default font
    Q_INVOKABLE static double defaultFontDescent(int pointSize);

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    static bool isMobile() { return true; }

    static bool fakeMobile() { return false; }
#else
    static bool isMobile() { return fakeMobile(); }

    static bool fakeMobile();
#endif

#if defined(Q_OS_ANDROID)
    static bool isAndroid() { return true; }

    static bool isiOS() { return false; }

    static bool isLinux() { return false; }

    static bool isMacOS() { return false; }

    static bool isWindows() { return false; }
#elif defined(Q_OS_IOS)
    static bool isAndroid() { return false; }

    static bool isiOS() { return true; }

    static bool isLinux() { return false; }

    static bool isMacOS() { return false; }

    static bool isWindows() { return false; }
#elif defined(Q_OS_MACOS)
    static bool isAndroid() { return false; }

    static bool isiOS() { return false; }

    static bool isLinux() { return false; }

    static bool isMacOS() { return true; }

    static bool isWindows() { return false; }
#elif defined(Q_OS_LINUX)
    static bool isAndroid() { return false; }

    static bool isiOS() { return false; }

    static bool isLinux() { return true; }

    static bool isMacOS() { return false; }

    static bool isWindows() { return false; }
#elif defined(Q_OS_WIN)
    static bool isAndroid() { return false; }

    static bool isiOS() { return false; }

    static bool isLinux() { return false; }

    static bool isMacOS() { return false; }

    static bool isWindows() { return true; }
#else
    static bool isAndroid() { return false; }

    static bool isiOS() { return false; }

    static bool isLinux() { return false; }

    static bool isMacOS() { return false; }

    static bool isWindows() { return false; }
#endif

#if defined(QGC_NO_SERIAL_LINK)
    static bool isSerialAvailable() { return false; }
#else
    static bool isSerialAvailable() { return true; }
#endif

#ifdef QT_DEBUG
    static bool isDebug() { return true; }
#else
    static bool isDebug() { return false; }
#endif

    static bool hasTouch();

    /// One line per input device Qt can see, with the type it was classified as. A touch screen
    /// Windows never reports as a TouchScreen is a touch screen no touch-filtered handler matches.
    Q_INVOKABLE static QString inputDeviceSummary();

    static QString iOSDevice();
    static QString fixedFontFamily();
    static QString normalFontFamily();

signals:
    void probeLogChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void _recordProbeEvent(QEvent* event);
    static QString _probeDeviceTypeName(QInputDevice::DeviceType type);
    static QString _probeEventName(QEvent::Type type);

    QString _probeLog;
    int _probeCount = 0;
};
