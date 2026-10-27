# Hop Xray bindings

Derived from amnezia-vpn/amnezia-xray-bindings v1.4.0 (GPL-3.0).
Source ZIP SHA256: 8977896bba99f1a3bad61d734b2929ec3d01c3ca0e206ee8ce5eb013d38ab118.
Xray core is pinned by go.mod/go.sum to amnezia-xray-core v1.260728.0.

Hop adds lifetime-bound, per-endpoint Direct callbacks and registers the socket
controller once. Existing C ABI functions are retained. The host provides the
endpoint permit before a Direct dial and removes it when that socket closes.
UDP permits cannot expand to another destination. VPN outbounds do not request
Direct permission. No URL, credentials, or configuration are logged here.
