package org.amnezia.vpn

import android.content.Context
import java.io.File
import java.io.IOException
import java.util.UUID

internal object VpnConfigHandoff {
    const val KEY = "VPN_CONFIG_HANDOFF"
    private const val MAX_AGE_MS = 5 * 60 * 1000L
    private val tokenPattern = Regex("[0-9a-f-]{36}")

    private fun directory(context: Context) = File(context.cacheDir, "vpn-config-handoff")

    fun store(context: Context, config: String): String {
        val directory = directory(context)
        if (!directory.isDirectory && !directory.mkdirs()) throw IOException("VPN handoff directory unavailable")
        val now = System.currentTimeMillis()
        directory.listFiles()?.forEach {
            if (now - it.lastModified() > MAX_AGE_MS) it.delete()
        }
        val token = UUID.randomUUID().toString()
        val file = File(directory, token)
        try {
            file.writeText(config)
        } catch (error: IOException) {
            file.delete()
            throw error
        }
        return token
    }

    fun take(context: Context, token: String): String {
        if (!tokenPattern.matches(token)) throw IOException("Invalid VPN handoff token")
        val file = File(directory(context), token)
        try {
            return file.readText()
        } finally {
            file.delete()
        }
    }

    fun discard(context: Context, token: String) {
        if (tokenPattern.matches(token)) File(directory(context), token).delete()
    }
}
