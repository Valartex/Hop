package libXray

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"io"
	"net"
	"net/http"
	"net/url"
	"regexp"
	"sync/atomic"
	"syscall"
	"time"

	awgprobe "github.com/amnezia-vpn/amnezia-libxray/hopprobe"
	xnet "github.com/xtls/xray-core/common/net"
	"github.com/xtls/xray-core/core"
	"github.com/xtls/xray-core/transport/internet"
)

type hopCounters struct{ vpnRx, vpnTx, directRx, directTx atomic.Int64 }

var hopTraffic hopCounters
var hopAwg *awgprobe.Session

type hopDialer struct {
	controller DialerController
	awg        *awgprobe.Session
}

func (dialer *hopDialer) DestIpAddress() net.IP { return nil }

func (dialer *hopDialer) Dial(ctx context.Context, source xnet.Address, destination xnet.Destination, options *internet.SocketConfig) (net.Conn, error) {
	route := "hop-vpn"
	if options != nil && options.Interface != "" {
		route = options.Interface
	}
	var connection net.Conn
	var err error
	switch route {
	case "hop-awg":
		if dialer.awg == nil {
			return nil, errors.New("AWG session unavailable")
		}
		connection, err = dialer.awg.DialContext(ctx, destination.Network.SystemString(), destination.NetAddr())
	case "hop-vpn", "hop-direct":
		native := net.Dialer{Timeout: 15 * time.Second, KeepAlive: 30 * time.Second}
		native.Control = func(_, _ string, raw syscall.RawConn) error {
			protected := false
			if err := raw.Control(func(fd uintptr) { protected = dialer.controller.ProtectFd(int(fd)) }); err != nil {
				return err
			}
			if !protected {
				return errors.New("socket protection failed")
			}
			return nil
		}
		if source != nil && source.Family().IsIP() && !source.IP().IsUnspecified() {
			if destination.Network == xnet.Network_UDP {
				native.LocalAddr = &net.UDPAddr{IP: source.IP()}
			} else {
				native.LocalAddr = &net.TCPAddr{IP: source.IP()}
			}
		}
		connection, err = native.DialContext(ctx, destination.Network.SystemString(), destination.NetAddr())
	default:
		return nil, errors.New("unsupported socket interface")
	}
	if err != nil {
		return nil, err
	}
	if route == "hop-direct" {
		return &hopCountedConnection{Conn: connection, received: &hopTraffic.directRx, sent: &hopTraffic.directTx}, nil
	}
	return &hopCountedConnection{Conn: connection, received: &hopTraffic.vpnRx, sent: &hopTraffic.vpnTx}, nil
}

type hopCountedConnection struct {
	net.Conn
	received, sent *atomic.Int64
}

func (connection *hopCountedConnection) Read(buffer []byte) (int, error) {
	n, err := connection.Conn.Read(buffer)
	connection.received.Add(int64(n))
	return n, err
}
func (connection *hopCountedConnection) Write(buffer []byte) (int, error) {
	n, err := connection.Conn.Write(buffer)
	connection.sent.Add(int64(n))
	return n, err
}
func (connection *hopCountedConnection) ReadFrom(buffer []byte) (int, net.Addr, error) {
	if packets, ok := connection.Conn.(net.PacketConn); ok {
		n, address, err := packets.ReadFrom(buffer)
		connection.received.Add(int64(n))
		return n, address, err
	}
	n, err := connection.Read(buffer)
	return n, connection.RemoteAddr(), err
}
func (connection *hopCountedConnection) WriteTo(buffer []byte, address net.Addr) (int, error) {
	if packets, ok := connection.Conn.(net.PacketConn); ok {
		n, err := packets.WriteTo(buffer, address)
		connection.sent.Add(int64(n))
		return n, err
	}
	if address.String() != connection.RemoteAddr().String() {
		return 0, errors.New("destination mismatch")
	}
	return connection.Write(buffer)
}

// Настраивается только до запуска Xray; закрывается после остановки всех его соединений.
func HopConfigureTransport(profile string, controller DialerController) string {
	if controller == nil || hopAwg != nil {
		return "transport state"
	}
	if profile != "" {
		session, err := awgprobe.OpenSession(profile, controller.ProtectFd)
		if err != nil {
			return "AWG " + err.Error()
		}
		hopAwg = session
	}
	hopTraffic.vpnRx.Store(0)
	hopTraffic.vpnTx.Store(0)
	hopTraffic.directRx.Store(0)
	hopTraffic.directTx.Store(0)
	internet.UseAlternativeSystemDialer(&hopDialer{controller: controller, awg: hopAwg})
	return ""
}

func HopCloseTransport() {
	internet.UseAlternativeSystemDialer(nil)
	if hopAwg != nil {
		hopAwg.Close()
		hopAwg = nil
	}
}

func HopTrafficStatistics() string {
	data, _ := json.Marshal(map[string]int64{"vpnRx": hopTraffic.vpnRx.Load(), "vpnTx": hopTraffic.vpnTx.Load(), "directRx": hopTraffic.directRx.Load(), "directTx": hopTraffic.directTx.Load()})
	return string(data)
}

func HopValidateDomainPatterns(input string) string {
	var patterns []string
	if len(input) > 2*1024*1024 || json.Unmarshal([]byte(input), &patterns) != nil {
		return `{"code":4,"valid":false}`
	}
	for _, pattern := range patterns {
		if len(pattern) < 8 || len(pattern) > 4096 || pattern[:7] != "regexp:" {
			return `{"code":4,"valid":false}`
		}
		if _, err := regexp.Compile(pattern[7:]); err != nil {
			return `{"code":4,"valid":false}`
		}
	}
	return `{"code":0,"valid":true}`
}

func HopProbeAwg(input string, controller DialerController) string {
	if controller == nil {
		return `{"code":4,"unsupported":true}`
	}
	var output bytes.Buffer
	code := awgprobe.RunProtected(bytes.NewBufferString(input), &output, controller.ProtectFd)
	result := map[string]any{}
	json.Unmarshal(output.Bytes(), &result)
	result["code"] = code
	data, _ := json.Marshal(result)
	return string(data)
}

func HopProbeXray(input, proxy string) string {
	fail := `{"code":3,"ok":false,"latencyMs":-1}`
	if len(input) > 65536 {
		return `{"code":4,"unsupported":true}`
	}
	config, err := core.LoadConfig("json", bytes.NewReader([]byte(input)))
	if err != nil {
		return `{"code":4,"unsupported":true}`
	}
	instance, err := core.New(config)
	if err != nil {
		return `{"code":4,"unsupported":true}`
	}
	defer instance.Close()
	if instance.Start() != nil {
		return fail
	}
	address, err := url.Parse(proxy)
	if err != nil || address.Scheme != "socks5" || address.Hostname() != "127.0.0.1" {
		return fail
	}
	transport := &http.Transport{Proxy: http.ProxyURL(address), DisableKeepAlives: true}
	defer transport.CloseIdleConnections()
	client := &http.Client{Transport: transport, Timeout: 10 * time.Second,
		CheckRedirect: func(*http.Request, []*http.Request) error { return http.ErrUseLastResponse }}
	started := time.Now()
	response, err := client.Get("https://www.gstatic.com/generate_204")
	if err != nil {
		return fail
	}
	defer response.Body.Close()
	size, err := io.Copy(io.Discard, io.LimitReader(response.Body, 1025))
	if err != nil || response.StatusCode != 204 || size != 0 {
		return fail
	}
	data, _ := json.Marshal(map[string]any{"code": 0, "ok": true, "latencyMs": time.Since(started).Milliseconds()})
	return string(data)
}
