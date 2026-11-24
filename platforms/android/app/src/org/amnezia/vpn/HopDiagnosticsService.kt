package org.amnezia.vpn

import android.app.Service
import android.content.Intent
import android.os.Handler
import android.os.HandlerThread
import android.os.IBinder
import android.os.Looper
import android.os.Message
import android.os.Messenger
import android.os.ParcelFileDescriptor
import android.os.Process
import go.Seq
import java.net.ServerSocket
import java.util.UUID
import org.amnezia.vpn.protocol.xray.libXray.DialerController
import org.amnezia.vpn.protocol.xray.libXray.LibXray
import org.json.JSONArray
import org.json.JSONObject

class HopDiagnosticsService : Service() {
    private val worker = HandlerThread("HopDiagnostics")
    private lateinit var work: Handler
    private val messenger = Messenger(Handler(Looper.getMainLooper()) { message ->
        when (message.what) {
            1 -> {
                val role = message.data.getString("role") ?: ""
                val input = message.data.getString("input") ?: ""
                val reply = message.replyTo
                work.post {
                    val result = try { execute(role, input) } catch (_: Exception) { "{\"code\":3,\"ok\":false}" }
                    try { reply.send(Message.obtain(null, 1).apply { data.putString("result", result) }) } catch (_: Exception) { }
                }
            }
            2 -> Process.killProcess(Process.myPid())
        }
        true
    })

    override fun onCreate() { super.onCreate(); worker.start(); work = Handler(worker.looper); Seq.setContext(this) }
    override fun onBind(intent: Intent): IBinder = messenger.binder
    override fun onDestroy() { worker.quitSafely(); super.onDestroy(); Process.killProcess(Process.myPid()) }

    private fun execute(role: String, input: String): String {
        if (input.length > 65536 && role != "validate") return "{\"code\":4,\"unsupported\":true}"
        if (role == "validate") return LibXray.hopValidateDomainPatterns(JSONObject(input).getJSONArray("patterns").toString())
        val network = HopNetworkTasks.physicalNetwork() ?: return "{\"code\":3,\"ok\":false}"
        val controller = DialerController { fd ->
            try { ParcelFileDescriptor.fromFd(fd.toInt()).use { network.bindSocket(it.fileDescriptor) }; true } catch (_: Exception) { false }
        }
        if (role.endsWith("hop_awg_probe")) return LibXray.hopProbeAwg(input, controller)
        val config = JSONObject(input)
        val port = ServerSocket(0, 1, java.net.InetAddress.getByName("127.0.0.1")).use { it.localPort }
        val user = UUID.randomUUID().toString()
        val password = UUID.randomUUID().toString()
        config.put("log", JSONObject().put("loglevel", "none"))
        val account = JSONObject().put("user", user).put("pass", password)
        val settings = JSONObject().put("auth", "password").put("accounts", JSONArray().put(account))
        val inbound = JSONObject().put("listen", "127.0.0.1").put("port", port).put("protocol", "socks").put("settings", settings)
        config.put("inbounds", JSONArray().put(inbound))
        config.remove("routing"); config.remove("dns"); config.remove("api"); config.remove("metrics")
        val outbounds = config.getJSONArray("outbounds")
        if (outbounds.length() == 0 || outbounds.getJSONObject(0).optString("protocol") != "vless") return "{\"code\":4,\"unsupported\":true}"
        val assets = getDir("assets", MODE_PRIVATE).absolutePath
        LibXray.initXray(assets)
        try {
            if (!LibXray.hopConfigureTransport("", controller).isNullOrEmpty()) return "{\"code\":3,\"ok\":false}"
            return LibXray.hopProbeXray(config.toString(), "socks5://$user:$password@127.0.0.1:$port")
        } finally { LibXray.hopCloseTransport() }
    }
}
