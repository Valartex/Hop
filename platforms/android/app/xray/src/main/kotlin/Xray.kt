package org.amnezia.vpn.protocol.xray

import android.content.Context
import android.net.VpnService.Builder
import java.io.File
import java.io.IOException
import java.net.InetAddress
import java.net.ServerSocket
import java.util.UUID
import go.Seq
import org.amnezia.vpn.protocol.BadConfigException
import org.amnezia.vpn.protocol.Protocol
import org.amnezia.vpn.protocol.ProtocolState.CONNECTED
import org.amnezia.vpn.protocol.ProtocolState.DISCONNECTED
import org.amnezia.vpn.protocol.Statistics
import org.amnezia.vpn.protocol.VpnStartException
import org.amnezia.vpn.protocol.xray.libXray.DialerController
import org.amnezia.vpn.protocol.xray.libXray.LibXray
import org.amnezia.vpn.protocol.xray.libXray.Logger
import org.amnezia.vpn.protocol.xray.libXray.Tun2SocksConfig
import org.amnezia.vpn.util.Log
import org.amnezia.vpn.util.net.InetNetwork
import org.amnezia.vpn.util.net.ip
import org.amnezia.vpn.util.net.parseInetAddress
import org.json.JSONArray
import org.json.JSONObject

private const val TAG = "Xray"
private const val LIBXRAY_TAG = "libXray"

private fun findSocksInboundIndex(inbounds: JSONArray): Int {
    for (i in 0 until inbounds.length()) {
        val o = inbounds.optJSONObject(i) ?: continue
        if (o.optString("protocol").equals("socks", ignoreCase = true)) {
            return i
        }
    }
    return -1
}

private fun acquireFreeLocalPort(): Int {
    try {
        ServerSocket(0, 1, InetAddress.getByName("127.0.0.1")).use { return it.localPort }
    } catch (e: Exception) {
        throw VpnStartException(
            "Failed to acquire free TCP port on 127.0.0.1 for SOCKS inbound: ${e.message}"
        )
    }
}

class Xray : Protocol() {

    private var isRunning: Boolean = false
    override val statistics: Statistics
        get() {
            if (!isRunning) return Statistics.EMPTY_STATISTICS
            val counters = JSONObject(LibXray.hopTrafficStatistics())
            return Statistics.build {
                setRxBytes(counters.optLong("vpnRx")); setTxBytes(counters.optLong("vpnTx"))
                setDirectRxBytes(counters.optLong("directRx")); setDirectTxBytes(counters.optLong("directTx"))
            }
        }

    override fun internalInit() {
        Seq.setContext(context)
        if (!isInitialized) {
            LibXray.initLogger(object : Logger {
                override fun warning(s: String) = Log.w(LIBXRAY_TAG, s)

                override fun error(s: String) = Log.e(LIBXRAY_TAG, s)

                override fun write(msg: ByteArray): Long {
                    Log.w(LIBXRAY_TAG, String(msg))
                    return msg.size.toLong()
                }
            }).isNotNullOrBlank { err ->
                Log.w(TAG, "Failed to initialize logger: $err")
            }
        }
    }

    override suspend fun startVpn(config: JSONObject, vpnBuilder: Builder, protect: (Int) -> Boolean) {
        if (isRunning) {
            Log.w(TAG, "XRay already running")
            return
        }

        val xrayConfigData = config.optJSONObject("xray_config_data")
            ?: config.optJSONObject("ssxray_config_data")
            ?: throw BadConfigException("config_data not found")
        val xrayJsonConfig = JSONObject(xrayConfigData.optString("config"))
        val outbounds = xrayJsonConfig.getJSONArray("outbounds")
        val ipv6BlockTag = "hop-platform-ipv6-block"
        if ((0 until outbounds.length()).any { outbounds.getJSONObject(it).optString("tag") == ipv6BlockTag }) {
            throw BadConfigException("Зарезервированное имя выхода профиля")
        }
        outbounds.put(JSONObject().put("tag", ipv6BlockTag).put("protocol", "blackhole"))
        val routing = xrayJsonConfig.optJSONObject("routing") ?: JSONObject().also { xrayJsonConfig.put("routing", it) }
        val previousRules = routing.optJSONArray("rules") ?: JSONArray()
        val rules = JSONArray().put(JSONObject().put("type", "field").put("ip", JSONArray().put("::/0")).put("outboundTag", ipv6BlockTag))
        for (index in 0 until previousRules.length()) rules.put(previousRules.get(index))
        routing.put("rules", rules)
        for (index in 0 until outbounds.length()) {
            val outbound = outbounds.getJSONObject(index)
            val tag = outbound.optString("tag")
            if (index == 0 || tag == "hop-vpn" || tag == "hop-direct") {
                val stream = outbound.optJSONObject("streamSettings") ?: JSONObject().also { outbound.put("streamSettings", it) }
                val options = stream.optJSONObject("sockopt") ?: JSONObject().also { stream.put("sockopt", it) }
                if (!options.has("interface")) options.put("interface", if (tag == "hop-direct") "hop-direct" else "hop-vpn")
            }
        }

        // Inject SOCKS5 auth before starting xray. Re-uses existing credentials if present.
        ensureInboundAuth(xrayJsonConfig)
        val xrayConfig = parseConfig(config, xrayJsonConfig)

        start(xrayConfig, xrayJsonConfig.toString(), vpnBuilder, protect, config.optString("hopAwgConfig"))
        state.value = CONNECTED
        isRunning = true
    }

    private fun parseConfig(config: JSONObject, xrayJsonConfig: JSONObject): XrayConfig {
        return XrayConfig.build {
            addAddress(XrayConfig.DEFAULT_IPV4_ADDRESS)

            config.optString("dns1").let {
                if (it.isNotBlank()) addDnsServer(parseInetAddress(it))
            }

            config.optString("dns2").let {
                if (it.isNotBlank()) addDnsServer(parseInetAddress(it))
            }

            addRoute(InetNetwork("0.0.0.0", 0))
            addRoute(InetNetwork("::", 0))
            config.getString("hostName").let {
                excludeRoute(InetNetwork(it, 32))
            }

            config.optString("mtu").let {
                if (it.isNotBlank()) setMtu(it.toInt())
            }

            val inbounds = xrayJsonConfig.getJSONArray("inbounds")
            val socksIdx = findSocksInboundIndex(inbounds)
            if (socksIdx < 0) {
                throw BadConfigException("socks inbound not found")
            }
            val socksConfig = inbounds.getJSONObject(socksIdx)
            socksConfig.getInt("port").let { setSocksPort(it) }

            val socksSettings = socksConfig.optJSONObject("settings")
            val accounts = socksSettings?.optJSONArray("accounts")
            if (accounts != null && accounts.length() > 0) {
                val account = accounts.getJSONObject(0)
                setSocksUser(account.optString("user"))
                setSocksPass(account.optString("pass"))
            }

            configSplitTunneling(config)
            configAppSplitTunneling(config)
        }
    }

    private fun start(config: XrayConfig, configJson: String, vpnBuilder: Builder, protect: (Int) -> Boolean, awgProfile: String) {
        buildVpnInterface(config, vpnBuilder)

        LibXray.hopConfigureTransport(awgProfile, DialerController { protect(it.toInt()) }).isNotNullOrBlank {
            throw VpnStartException("Не удалось подготовить защищённый транспорт")
        }

        try {
            Log.d(TAG, "Run XRay")
            Log.i(TAG, "xray ${LibXray.xrayVersion()}")
            val assetsPath = context.getDir("assets", Context.MODE_PRIVATE).absolutePath
            LibXray.initXray(assetsPath)
            val geoDir = File(assetsPath, "geo").absolutePath
            val configPath = File(context.cacheDir, "config.json")
            Log.v(TAG, "xray.location.asset: $geoDir")
            Log.v(TAG, "config: $configPath")
            try {
                configPath.writeText(configJson)
                LibXray.runXray(geoDir, configPath.absolutePath, config.maxMemory).isNotNullOrBlank {
                    throw VpnStartException("Не удалось запустить Xray")
                }
            } catch (e: IOException) {
                throw VpnStartException("Failed to write xray config: ${e.message}")
            } finally { configPath.delete() }

            // Первые пакеты VPN должны поступать после запуска локального SOCKS-входа.
            vpnBuilder.establish().use { tunFd ->
                if (tunFd == null) {
                    throw VpnStartException("Create VPN interface: permission not granted or revoked")
                }
                Log.d(TAG, "Run tun2Socks")
                runTun2Socks(config, tunFd.detachFd())
            }
        } catch (failure: Exception) {
            LibXray.stopTun2Socks()
            LibXray.stopXray()
            LibXray.hopCloseTransport()
            throw failure
        }
    }

    override fun stopVpn() {
        LibXray.stopXray().isNotNullOrBlank { err ->
            Log.e(TAG, "Failed to stop XRay: $err")
        }
        LibXray.stopTun2Socks().isNotNullOrBlank { err ->
            Log.e(TAG, "Failed to stop tun2Socks: $err")
        }
        LibXray.hopCloseTransport()

        isRunning = false
        state.value = DISCONNECTED
    }

    override fun reconnectVpn(vpnBuilder: Builder, protect: (Int) -> Boolean) {
        state.value = CONNECTED
    }

    private fun runTun2Socks(config: XrayConfig, fd: Int) {
        val proxyUrl = "socks5://${config.socksUser}:${config.socksPass}@127.0.0.1:${config.socksPort}"
        val tun2SocksConfig = Tun2SocksConfig().apply {
            mtu = config.mtu.toLong()
            proxy = proxyUrl
            device = "fd://$fd"
            logLevel = "warn"
        }
        LibXray.startTun2Socks(tun2SocksConfig, fd.toLong()).isNotNullOrBlank { err ->
            throw VpnStartException("Failed to start tun2socks: $err")
        }
    }

    // Ensures SOCKS5 auth is present on the socks inbound settings.
    // Re-uses existing credentials if already configured; otherwise generates random ones.
    private fun ensureInboundAuth(xrayConfig: JSONObject) {
        val inbounds = xrayConfig.optJSONArray("inbounds") ?: return
        val socksIdx = findSocksInboundIndex(inbounds)
        if (socksIdx < 0) return

        val inbound = inbounds.getJSONObject(socksIdx)
        inbound.put("port", acquireFreeLocalPort())
        val settings = inbound.optJSONObject("settings") ?: JSONObject().also { inbound.put("settings", it) }
        val accounts = settings.optJSONArray("accounts")
        if (accounts != null && accounts.length() > 0) {
            val account = accounts.getJSONObject(0)
            if (account.optString("user").isNotEmpty() && account.optString("pass").isNotEmpty()) {
                // Ensure auth mode is enforced even for imported configs that had accounts
                // but auth: "noauth" (or no auth field).
                settings.put("auth", "password")
                inbound.put("settings", settings)
                inbounds.put(socksIdx, inbound)
                return
            }
        }

        val user = UUID.randomUUID().toString().replace("-", "").substring(0, 16)
        val pass = UUID.randomUUID().toString().replace("-", "")
        settings.put("auth", "password")
        settings.put("accounts", JSONArray().put(JSONObject().put("user", user).put("pass", pass)))
        inbound.put("settings", settings)
        inbounds.put(socksIdx, inbound)
    }

    companion object {
        val instance: Xray by lazy { Xray() }
    }
}

private fun String?.isNotNullOrBlank(block: (String) -> Unit) {
    if (!this.isNullOrBlank()) {
        block(this)
    }
}
