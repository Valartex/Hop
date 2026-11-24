package org.amnezia.vpn.protocol

import android.os.Bundle

private const val RX_BYTES_KEY = "rxBytes"
private const val TX_BYTES_KEY = "txBytes"

@Suppress("DataClassPrivateConstructor")
data class Statistics private constructor(
    val rxBytes: Long = 0L,
    val txBytes: Long = 0L,
    val directRxBytes: Long = 0L,
    val directTxBytes: Long = 0L
) {

    private constructor(builder: Builder) : this(builder.rxBytes, builder.txBytes, builder.directRxBytes, builder.directTxBytes)

    @Suppress("SuspiciousEqualsCombination")
    fun isEmpty(): Boolean = this === EMPTY_STATISTICS || this == EMPTY_STATISTICS

    class Builder {
        var rxBytes: Long = 0L
            private set

        var txBytes: Long = 0L
            private set
        var directRxBytes: Long = 0L
            private set
        var directTxBytes: Long = 0L
            private set

        fun setRxBytes(rxBytes: Long) = apply { this.rxBytes = rxBytes }
        fun setTxBytes(txBytes: Long) = apply { this.txBytes = txBytes }
        fun setDirectRxBytes(value: Long) = apply { directRxBytes = value }
        fun setDirectTxBytes(value: Long) = apply { directTxBytes = value }

        fun build(): Statistics =
            if (rxBytes + txBytes + directRxBytes + directTxBytes == 0L) EMPTY_STATISTICS
            else Statistics(this)
    }

    companion object {
        val EMPTY_STATISTICS: Statistics = Statistics()

        inline fun build(block: Builder.() -> Unit): Statistics = Builder().apply(block).build()
    }
}

fun Bundle.putStatistics(statistics: Statistics) {
    putLong(RX_BYTES_KEY, statistics.rxBytes)
    putLong(TX_BYTES_KEY, statistics.txBytes)
    putLong("directRxBytes", statistics.directRxBytes)
    putLong("directTxBytes", statistics.directTxBytes)
}

fun Bundle.getStatistics(): Statistics =
    Statistics.build {
        setRxBytes(getLong(RX_BYTES_KEY))
        setTxBytes(getLong(TX_BYTES_KEY))
        setDirectRxBytes(getLong("directRxBytes"))
        setDirectTxBytes(getLong("directTxBytes"))
    }
