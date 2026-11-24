package org.amnezia.vpn

import android.content.Context
import android.content.Intent
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.os.SystemClock
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.Inet4Address
import java.net.HttpURLConnection
import java.net.Proxy
import java.net.URL
import java.security.SecureRandom
import java.util.concurrent.ExecutorCompletionService
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import org.json.JSONArray
import org.json.JSONObject

object HopNetworkTasks {
    private lateinit var context: Context
    @Volatile private var preferredNetwork: Network? = null
    private val httpsProbeExecutor = Executors.newFixedThreadPool(2)
    private val httpsProbeTargets = listOf(
        "https://www.google.com/generate_204" to HttpURLConnection.HTTP_NO_CONTENT,
        "https://www.cloudflare.com/cdn-cgi/trace" to HttpURLConnection.HTTP_OK
    )

    fun initialize(application: Context) {
        context = application.applicationContext
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            val request = NetworkRequest.Builder()
                .addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
                .addCapability(NetworkCapabilities.NET_CAPABILITY_NOT_VPN).build()
            context.getSystemService(ConnectivityManager::class.java).registerBestMatchingNetworkCallback(
                request, object : ConnectivityManager.NetworkCallback() {
                    override fun onAvailable(network: Network) { preferredNetwork = network }
                    override fun onLost(network: Network) { if (preferredNetwork == network) preferredNetwork = null }
                }, Handler(Looper.getMainLooper())
            )
        }
    }

    fun physicalNetwork(): Network? {
        val manager = context.getSystemService(ConnectivityManager::class.java)
        fun usable(network: Network): Boolean {
            val capabilities = manager.getNetworkCapabilities(network) ?: return false
            return capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) &&
                !capabilities.hasTransport(NetworkCapabilities.TRANSPORT_VPN)
        }
        manager.activeNetwork?.let { if (usable(it)) return it }
        preferredNetwork?.let { if (usable(it)) return it }
        val networks = manager.allNetworks.filter { usable(it) }
        return networks.firstOrNull {
            manager.getNetworkCapabilities(it)?.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) == true
        } ?: networks.firstOrNull()
    }

    @JvmStatic fun networkIdentity(): String = physicalNetwork()?.networkHandle?.toString() ?: ""

    @JvmStatic fun openVpnSettings() {
        context.startActivity(Intent(Settings.ACTION_VPN_SETTINGS).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
    }

    @JvmStatic fun execute(role: String, input: String): String = try {
        val request = JSONObject(input)
        when {
            request.optString("action") == "resolve" -> {
                val addresses = physicalNetwork()?.getAllByName(request.getString("host"))
                    ?.filterIsInstance<Inet4Address>()?.map { it.hostAddress } ?: emptyList()
                JSONObject().put("code", if (addresses.isEmpty()) 3 else 0).put("addresses", JSONArray(addresses)).toString()
            }
            role.endsWith("hop_tunnel_probe") -> checkTunnel(request).toString()
            else -> HopDiagnosticsClient.execute(context, role, input)
        }
    } catch (_: Exception) {
        // Адреса, ключи и сообщения библиотек не включаются в диагностический ответ.
        JSONObject().put("code", 3).put("ok", false).put("latencyMs", -1).toString()
    }

    private fun checkTunnel(request: JSONObject): JSONObject {
        val manager = context.getSystemService(ConnectivityManager::class.java)
        val network = manager.allNetworks.firstOrNull {
            manager.getNetworkCapabilities(it)?.hasTransport(NetworkCapabilities.TRANSPORT_VPN) == true
        } ?: return JSONObject().put("code", 3).put("ok", false).put("latencyMs", -1)
        if (request.optBoolean("https")) return checkTunnelHttps(network)
        val resolver = network.getAllByName(request.getString("resolver")).firstOrNull { it is Inet4Address }
            ?: return JSONObject().put("code", 4).put("supported", false)
        val random = ByteArray(2).also { SecureRandom().nextBytes(it) }
        val query = byteArrayOf(random[0], random[1], 1, 0, 0, 1, 0, 0, 0, 0, 0, 0) +
            byteArrayOf(10) + "hop-health".toByteArray(Charsets.US_ASCII) + byteArrayOf(7) +
            "example".toByteArray(Charsets.US_ASCII) + byteArrayOf(3) +
            "com".toByteArray(Charsets.US_ASCII) + byteArrayOf(0, 0, 1, 0, 1)
        val started = SystemClock.elapsedRealtime()
        DatagramSocket(null).use { socket ->
            // Привязка к VPN обязательна: ответ через физическую сеть не является проверкой туннеля.
            network.bindSocket(socket)
            socket.soTimeout = 2800
            socket.connect(resolver, 53)
            repeat(3) {
                try {
                    socket.send(DatagramPacket(query, query.size))
                    val answer = DatagramPacket(ByteArray(4096), 4096)
                    socket.receive(answer)
                    val bytes = answer.data
                    val response = answer.length >= query.size && bytes[0] == random[0] && bytes[1] == random[1] &&
                        bytes[2].toInt() and 0x80 != 0 && bytes[2].toInt() and 0x02 == 0 &&
                        bytes[5].toInt() == 1 && (bytes[3].toInt() and 0x0f) in listOf(0, 3) &&
                        bytes.copyOfRange(12, query.size).contentEquals(query.copyOfRange(12, query.size))
                    if (response) return JSONObject().put("code", 0).put("ok", true)
                        .put("latencyMs", SystemClock.elapsedRealtime() - started)
                } catch (_: java.net.SocketTimeoutException) { }
            }
        }
        return JSONObject().put("code", 3).put("ok", false).put("latencyMs", -1)
    }

    private fun checkTunnelHttps(network: Network): JSONObject {
        // Независимые адреса проверяются одновременно, чтобы сбой DNS или одного сайта не оборвал рабочий туннель.
        val started = SystemClock.elapsedRealtime()
        val deadline = started + 8000
        val completed = ExecutorCompletionService<Boolean>(httpsProbeExecutor)
        val probes = httpsProbeTargets.map { (url, expectedStatus) ->
            completed.submit { probeHttps(network, url, expectedStatus) }
        }
        try {
            repeat(probes.size) {
                val remaining = deadline - SystemClock.elapsedRealtime()
                if (remaining <= 0) return@repeat
                val result = completed.poll(remaining, TimeUnit.MILLISECONDS)
                if (result?.get() == true) return JSONObject().put("code", 0).put("ok", true)
                    .put("latencyMs", SystemClock.elapsedRealtime() - started)
            }
        } finally {
            probes.forEach { it.cancel(true) }
        }
        return JSONObject().put("code", 3).put("ok", false).put("latencyMs", -1)
    }

    private fun probeHttps(network: Network, url: String, expectedStatus: Int): Boolean = try {
        val connection = network.openConnection(URL(url), Proxy.NO_PROXY) as HttpURLConnection
        try {
            connection.connectTimeout = 3000
            connection.readTimeout = 3000
            connection.instanceFollowRedirects = false
            connection.useCaches = false
            connection.responseCode == expectedStatus
        } finally {
            connection.disconnect()
        }
    } catch (_: java.io.IOException) {
        false
    }
}
