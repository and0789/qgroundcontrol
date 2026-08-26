#include "ScreenToolsController.h"

#include <QtCore/QEvent>
#include <QtCore/QMetaEnum>
#include <QtCore/QStringList>
#include <QtGui/QCursor>
#include <QtGui/QFontDatabase>
#include <QtGui/QFontMetrics>
#include <QtGui/QInputDevice>
#include <QtGui/QPointerEvent>

#include "AppSettings.h"
#include "QGCApplication.h"
#include "QGCLoggingCategory.h"
#include "SettingsManager.h"

#if defined(Q_OS_IOS)
#include <sys/utsname.h>
#endif

QGC_LOGGING_CATEGORY(ScreenToolsControllerLog, "QMLControls.ScreenToolsController")

ScreenToolsController::ScreenToolsController(QObject* parent) : QObject(parent)
{
    // qCDebug(ScreenToolsControllerLog) << Q_FUNC_INFO << this;
}

ScreenToolsController::~ScreenToolsController()
{
    // qCDebug(ScreenToolsControllerLog) << Q_FUNC_INFO << this;
}

int ScreenToolsController::mouseX()
{
    return QCursor::pos().x();
}

int ScreenToolsController::mouseY()
{
    return QCursor::pos().y();
}

bool ScreenToolsController::hasTouch()
{
    for (const auto& inputDevice : QInputDevice::devices()) {
        if (inputDevice->type() == QInputDevice::DeviceType::TouchScreen) {
            return true;
        }
    }
    return false;
}

QString ScreenToolsController::_probeDeviceTypeName(QInputDevice::DeviceType type)
{
    switch (type) {
        case QInputDevice::DeviceType::Mouse:
            return QStringLiteral("Mouse");
        case QInputDevice::DeviceType::TouchScreen:
            return QStringLiteral("TouchScreen");
        case QInputDevice::DeviceType::TouchPad:
            return QStringLiteral("TouchPad");
        case QInputDevice::DeviceType::Puck:
            return QStringLiteral("Puck");
        case QInputDevice::DeviceType::Stylus:
            return QStringLiteral("Stylus");
        case QInputDevice::DeviceType::Airbrush:
            return QStringLiteral("Airbrush");
        case QInputDevice::DeviceType::Keyboard:
            return QStringLiteral("Keyboard");
        case QInputDevice::DeviceType::AllDevices:
            return QStringLiteral("AllDevices");
        case QInputDevice::DeviceType::Unknown:
            return QStringLiteral("Unknown");
    }
    return QStringLiteral("type=%1").arg(static_cast<int>(type));
}

QString ScreenToolsController::_probeEventName(QEvent::Type type)
{
    switch (type) {
        case QEvent::TouchBegin:
            return QStringLiteral("TouchBegin  ");
        case QEvent::TouchEnd:
            return QStringLiteral("TouchEnd    ");
        case QEvent::TouchCancel:
            return QStringLiteral("TouchCancel ");
        case QEvent::MouseButtonPress:
            return QStringLiteral("MousePress  ");
        case QEvent::MouseButtonRelease:
            return QStringLiteral("MouseRelease");
        case QEvent::TabletPress:
            return QStringLiteral("TabletPress ");
        case QEvent::TabletRelease:
            return QStringLiteral("TabletRelese");
        default:
            return QStringLiteral("event=%1").arg(static_cast<int>(type));
    }
}

void ScreenToolsController::startPointerProbe()
{
    qgcApp()->installEventFilter(this);
}

bool ScreenToolsController::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        case QEvent::TouchCancel:
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::TabletPress:
        case QEvent::TabletRelease:
            _recordProbeEvent(event);
            break;
        default:
            break;
    }

    return QObject::eventFilter(watched, event);
}

void ScreenToolsController::_recordProbeEvent(QEvent* event)
{
    // TouchUpdate arrives many times a second while a finger moves, and a wall of them buries the
    // press and release either side of it, which are the two this is being read for.
    if (event->type() == QEvent::TouchUpdate) {
        return;
    }

    QString line = _probeEventName(event->type());

    if (auto* pointerEvent = dynamic_cast<QPointerEvent*>(event)) {
        const QPointingDevice* const device = pointerEvent->pointingDevice();
        // The device behind a mouse event says whether Windows delivered a mouse or whether Qt made
        // one out of a touch that nothing claimed -- the same event either way, opposite causes.
        const QString deviceType = device ? _probeDeviceTypeName(device->type()) : QStringLiteral("no-device");
        const QString deviceName = device ? device->name() : QStringLiteral("?");

        QString where;
        if (pointerEvent->pointCount() > 0) {
            const QPointF position = pointerEvent->point(0).position();
            where = QStringLiteral(" @%1,%2").arg(qRound(position.x())).arg(qRound(position.y()));
        }

        line += QStringLiteral("  %1 \"%2\" pts=%3%4")
                    .arg(deviceType, deviceName)
                    .arg(pointerEvent->pointCount())
                    .arg(where);
    }

    _probeCount++;
    _probeLog = QStringLiteral("%1 %2\n%3").arg(_probeCount, 3).arg(line, _probeLog);

    // Keeps the readout to what fits on a small screen
    const QStringList lines = _probeLog.split(QLatin1Char('\n'));
    if (lines.size() > 14) {
        _probeLog = lines.mid(0, 14).join(QLatin1Char('\n'));
    }

    emit probeLogChanged();
}

QString ScreenToolsController::inputDeviceSummary()
{
    QStringList lines;
    for (const auto& inputDevice : QInputDevice::devices()) {
        lines.append(QStringLiteral("%1  \"%2\"").arg(_probeDeviceTypeName(inputDevice->type()), inputDevice->name()));
    }

    if (lines.isEmpty()) {
        return QStringLiteral("Qt reports no input devices at all");
    }
    return lines.join(QLatin1Char('\n'));
}

QString ScreenToolsController::iOSDevice()
{
#if defined(Q_OS_IOS)
    struct utsname systemInfo;
    uname(&systemInfo);
    return QString(systemInfo.machine);
#else
    return QString();
#endif
}

QString ScreenToolsController::fixedFontFamily()
{
    return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
}

QString ScreenToolsController::normalFontFamily()
{
    //-- See App.SettinsGroup.json for index
    const int langID = SettingsManager::instance()->appSettings()->qLocaleLanguage()->rawValue().toInt();
    if (langID == QLocale::Korean) {
        return QStringLiteral("NanumGothic");
    }

    return QStringLiteral("Open Sans");
}

double ScreenToolsController::defaultFontDescent(int pointSize)
{
    return QFontMetrics(QFont(normalFontFamily(), pointSize)).descent();
}

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
bool ScreenToolsController::fakeMobile()
{
    return qgcApp()->fakeMobile();
}
#endif
