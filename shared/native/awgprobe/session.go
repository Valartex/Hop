package awgprobe

import (
	"context"
	"errors"
	"fmt"
	"github.com/amnezia-vpn/amneziawg-go/v3/conn"
	"github.com/amnezia-vpn/amneziawg-go/v3/device"
	"github.com/amnezia-vpn/amneziawg-go/v3/tun/netstack"
	"net"
	"net/netip"
	"strings"
)

type protectedBind struct {
	conn.Bind
	protect func(int) bool
}

func newProtectedBind(protect func(int) bool) conn.Bind {
	if protect == nil {
		return conn.NewDefaultBind()
	}
	return &protectedBind{Bind: conn.NewDefaultBind(), protect: protect}
}

func (binding *protectedBind) Open(port uint16) ([]conn.ReceiveFunc, uint16, error) {
	receivers, actual, err := binding.Bind.Open(port)
	if err != nil {
		return nil, 0, err
	}
	sockets, ok := binding.Bind.(conn.PeekLookAtSocketFd)
	protected := 0
	if ok {
		for _, get := range []func() (int, error){sockets.PeekLookAtSocketFd4, sockets.PeekLookAtSocketFd6} {
			if fd, err := get(); err == nil && fd >= 0 {
				if !binding.protect(fd) {
					binding.Bind.Close()
					return nil, 0, errors.New("socket protection")
				}
				protected++
			}
		}
	}
	if protected == 0 {
		binding.Bind.Close()
		return nil, 0, errors.New("socket protection unavailable")
	}
	return receivers, actual, nil
}

// Session предоставляет стек AWG маршрутизатору Xray, не создавая второй системный VPN.
type Session struct {
	peer    *device.Device
	network *netstack.Net
}

func OpenSession(text string, protect func(int) bool) (*Session, error) {
	p, err := parseProfile(text)
	if err != nil {
		return nil, errors.New("profile")
	}
	tun, network, err := netstack.CreateNetTUN(p.addresses, []netip.Addr{p.dns}, p.mtu)
	if err != nil {
		return nil, errors.New("netstack")
	}
	peer := device.NewDevice(tun, newProtectedBind(protect), device.NewLogger(device.LogLevelSilent, ""))
	// Проверка использует только DNS-маршрут; рабочая сессия сохраняет AllowedIPs профиля.
	config := p.uapi
	var routes strings.Builder
	for _, prefix := range p.allowed {
		fmt.Fprintf(&routes, "allowed_ip=%s\n", prefix)
	}
	config = strings.Replace(config, fmt.Sprintf("allowed_ip=%s/32\n", p.dns), routes.String(), 1)
	if peer.IpcSet(config+"endpoint="+p.endpoint+"\n") != nil {
		peer.Close()
		return nil, errors.New("configure")
	}
	if peer.Up() != nil {
		peer.Close()
		return nil, errors.New("start")
	}
	return &Session{peer: peer, network: network}, nil
}

func (session *Session) DialContext(ctx context.Context, network, address string) (net.Conn, error) {
	return session.network.DialContext(ctx, network, address)
}

func (session *Session) Close() { session.peer.Close() }

func (session *Session) Statistics() string {
	value, _ := session.peer.IpcGet()
	var result strings.Builder
	for _, line := range strings.Split(value, "\n") {
		if strings.HasPrefix(line, "rx_bytes=") || strings.HasPrefix(line, "tx_bytes=") {
			result.WriteString(line + "\n")
		}
	}
	return result.String()
}
