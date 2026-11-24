#include "androidVpnProtocol.h"

#include "platforms/android/native/android_controller.h"
#include "platforms/android/native/routingAdapter.h"
#include <QJniObject>


AndroidVpnProtocol::AndroidVpnProtocol(const QJsonObject &configuration, QObject* parent)
    : VpnProtocol(configuration, parent)
{ }

ErrorCode AndroidVpnProtocol::start()
{
    qDebug() << "AndroidVpnProtocol::start()";
    if (!QJniObject::callStaticMethod<jboolean>("org/amnezia/vpn/HopDiagnosticsClient", "cancelForConnection", "()Z")) {
        return ErrorCode::InternalError;
    }
    const auto prepared = hop::prepareAndroidRouting(m_rawConfig);
    if (!prepared.isValid()) { return ErrorCode::InternalError; }
    return AndroidController::instance()->start(prepared.configuration);
}

void AndroidVpnProtocol::stop()
{
    qDebug() << "AndroidVpnProtocol::stop()";
    setConnectionState(Vpn::ConnectionState::Disconnecting);
    AndroidController::instance()->stop();
}
