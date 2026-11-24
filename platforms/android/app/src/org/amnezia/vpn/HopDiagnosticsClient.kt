package org.amnezia.vpn

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.os.Bundle
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.Message
import android.os.Messenger
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.locks.ReentrantLock
import org.json.JSONObject

object HopDiagnosticsClient {
    private val serial = ReentrantLock()
    private val guard = Any()
    private var epoch = 0L
    private var active: Request? = null
    private lateinit var context: Context
    @Volatile private var vpnActive = false
    private val mainHandler = Handler(Looper.getMainLooper())
    private var idleListener: (() -> Unit)? = null

    private class Request {
        val done = CountDownLatch(1)
        @Volatile var cancelled = false
        @Volatile var service: Messenger? = null
        @Volatile var result = "{\"code\":3,\"ok\":false}"
    }

    fun initialize(application: Context) { context = application.applicationContext }
    fun hasActiveSession(): Boolean = vpnActive
    fun setIdleListener(listener: (() -> Unit)?) { idleListener = listener }
    @JvmStatic fun setVpnActive(value: Boolean) {
        vpnActive = value
        if (!value) mainHandler.post { if (!vpnActive) idleListener?.invoke() }
    }

    @JvmStatic fun cancelForConnection(): Boolean {
        vpnActive = true
        return cancelDiagnostics()
    }

    @JvmStatic fun cancelDiagnostics(): Boolean {
        val request = synchronized(guard) { epoch++; active?.also { it.cancelled = true } } ?: return true
        try { request.service?.send(Message.obtain(null, 2)) } catch (_: Exception) { }
        // Подтверждение завершения процесса обязательно до повторного использования ключа AWG.
        return request.done.await(3, TimeUnit.SECONDS)
    }

    fun execute(application: Context, role: String, input: String): String {
        val generation = synchronized(guard) { epoch }
        if (!serial.tryLock(12, TimeUnit.SECONDS)) return "{\"code\":3,\"ok\":false}"
        try {
            val request = Request()
            synchronized(guard) {
                if (generation != epoch || (vpnActive && role.endsWith("hop_awg_probe"))) return "{\"code\":4,\"unsupported\":true}"
                active = request
            }
            val replies = Messenger(Handler(Looper.getMainLooper()) { message ->
                request.result = message.data.getString("result") ?: request.result
                request.done.countDown(); true
            })
            val connection = object : ServiceConnection {
                override fun onServiceConnected(name: ComponentName, binder: IBinder) {
                    request.service = Messenger(binder)
                    if (request.cancelled) { request.done.countDown(); return }
                    val message = Message.obtain(null, 1).apply {
                        replyTo = replies
                        data = Bundle().apply { putString("role", role); putString("input", input) }
                    }
                    try { request.service?.send(message) } catch (_: Exception) { request.done.countDown() }
                }
                override fun onServiceDisconnected(name: ComponentName) { request.done.countDown() }
                override fun onNullBinding(name: ComponentName) { request.done.countDown() }
                override fun onBindingDied(name: ComponentName) { request.done.countDown() }
            }
            var bound = false
            try {
                bound = application.bindService(Intent(application, HopDiagnosticsService::class.java), connection, Context.BIND_AUTO_CREATE)
                if (bound) request.done.await(13, TimeUnit.SECONDS)
                return request.result
            } finally {
                if (bound) application.unbindService(connection)
                synchronized(guard) { if (active === request) active = null }
            }
        } finally { serial.unlock() }
    }
}
