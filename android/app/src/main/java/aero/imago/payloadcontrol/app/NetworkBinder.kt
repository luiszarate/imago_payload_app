package aero.imago.payloadcontrol.app

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.wifi.WifiManager
import android.os.Build
import java.net.Inet4Address
import java.net.InetAddress

/**
 * Keeps the payload's traffic on the network the payload is on.
 *
 * A datalink's Wi-Fi (Herelink) has no internet, so Android keeps mobile data as the default
 * network and may send the app's packets there. Binding the process to the network whose
 * subnet holds the payload avoids that. A Wi-Fi lock keeps the radio out of power save, which
 * otherwise delivers video and telemetry in bursts.
 */
class NetworkBinder(context: Context) {
    private val connectivity = context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
    private val wifi = context.applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
    private var wifiLock: WifiManager.WifiLock? = null

    /** Binds to the network that reaches the first of `hosts` it can; returns its name, if any. */
    fun bindFor(hosts: List<String>): String? {
        val addresses = hosts.mapNotNull(::numericIPv4)
        val network = addresses.firstNotNullOfOrNull(::networkFor)
        connectivity.bindProcessToNetwork(network)
        acquireWifiLock()
        return network?.let { connectivity.getLinkProperties(it)?.interfaceName }
    }

    fun unbind() {
        connectivity.bindProcessToNetwork(null)
        wifiLock?.let { if (it.isHeld) it.release() }
        wifiLock = null
    }

    /** The network with the address on a directly connected subnet, or a specific route to it. */
    @Suppress("DEPRECATION") // allNetworks: the callback API that replaces it is overkill for one lookup
    private fun networkFor(address: InetAddress): Network? {
        for (network in connectivity.allNetworks) {
            val link = connectivity.getLinkProperties(network) ?: continue
            val onSubnet = link.linkAddresses.any { linkAddress ->
                val own = linkAddress.address
                own is Inet4Address && sameSubnet(own, address as Inet4Address, linkAddress.prefixLength)
            }
            if (onSubnet || link.routes.any { !it.isDefaultRoute && it.matches(address) }) {
                return network
            }
        }
        return null
    }

    @Suppress("DEPRECATION")
    private fun acquireWifiLock() {
        if (wifiLock != null) return
        val mode = if (Build.VERSION.SDK_INT >= 29) WifiManager.WIFI_MODE_FULL_LOW_LATENCY
                   else WifiManager.WIFI_MODE_FULL_HIGH_PERF
        wifiLock = wifi.createWifiLock(mode, "imago:payload").apply {
            setReferenceCounted(false)
            acquire()
        }
    }

    private companion object {
        /** Only literal IPv4 addresses: never a DNS lookup here. */
        fun numericIPv4(host: String): InetAddress? {
            val parts = host.trim().split(".")
            if (parts.size != 4 || parts.any { it.isEmpty() || it.length > 3 || !it.all(Char::isDigit) }) return null
            val bytes = parts.map { it.toInt() }
            if (bytes.any { it > 255 }) return null
            return InetAddress.getByAddress(bytes.map { it.toByte() }.toByteArray())
        }

        fun sameSubnet(a: Inet4Address, b: Inet4Address, prefix: Int): Boolean {
            if (prefix <= 0 || prefix > 32) return false
            val mask = if (prefix == 32) -1 else ((1L shl 32) - (1L shl (32 - prefix))).toInt()
            return (a.toInt() and mask) == (b.toInt() and mask)
        }

        fun Inet4Address.toInt(): Int = address.fold(0) { acc, byte -> (acc shl 8) or (byte.toInt() and 0xFF) }
    }
}
