package org.amnezia.vpn

import android.app.Service
import android.content.Intent
import android.os.Binder
import android.os.IBinder

// Обратная привязка от foreground VPN удерживает процесс общего контроллера при скрытом UI.
class HopRuntimeService : Service() {
    private val binder = Binder()
    override fun onBind(intent: Intent): IBinder = binder
}
