package awgprobe

import (
	"bytes"
	"context"
	"encoding/base64"
	"encoding/hex"
	"fmt"
	"io"
	"net/netip"
	"strings"
	"testing"
	"time"

	"github.com/amnezia-vpn/amneziawg-go/v3/conn"
	"github.com/amnezia-vpn/amneziawg-go/v3/device"
	"github.com/amnezia-vpn/amneziawg-go/v3/tun/netstack"
	"golang.org/x/crypto/curve25519"
)

// Два локальных зашифрованных узла проверяют рабочие маршруты без системного VPN.
func TestSessionCarriesTCPAndUDPOutsideDNSHost(t *testing.T) {
	serverKey := bytes.Repeat([]byte{17}, 32)
	clientKey := bytes.Repeat([]byte{29}, 32)
	serverPublic, _ := curve25519.X25519(serverKey, curve25519.Basepoint)
	clientPublic, _ := curve25519.X25519(clientKey, curve25519.Basepoint)
	tun, network, err := netstack.CreateNetTUN([]netip.Addr{
		netip.MustParseAddr("10.200.0.1"), netip.MustParseAddr("10.200.0.3"),
	}, nil, 1280)
	if err != nil {
		t.Fatal(err)
	}
	server := device.NewDevice(tun, conn.NewDefaultBind(), device.NewLogger(device.LogLevelSilent, ""))
	defer server.Close()
	if err := server.IpcSet(fmt.Sprintf("private_key=%s\nlisten_port=0\npublic_key=%s\nallowed_ip=10.200.0.2/32\n", hex.EncodeToString(serverKey), hex.EncodeToString(clientPublic))); err != nil {
		t.Fatal(err)
	}
	if err := server.Up(); err != nil {
		t.Fatal(err)
	}
	state, err := server.IpcGet()
	if err != nil {
		t.Fatal(err)
	}
	port := ""
	for _, line := range strings.Split(state, "\n") {
		if value, found := strings.CutPrefix(line, "listen_port="); found {
			port = value
		}
	}
	if port == "" || port == "0" {
		t.Fatal("missing local endpoint")
	}
	profile := fmt.Sprintf("[Interface]\nPrivateKey=%s\nAddress=10.200.0.2/32\nDNS=10.200.0.1\n[Peer]\nPublicKey=%s\nAllowedIPs=10.200.0.0/24\nEndpoint=127.0.0.1:%s\n", base64.StdEncoding.EncodeToString(clientKey), base64.StdEncoding.EncodeToString(serverPublic), port)
	session, err := OpenSession(profile, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer session.Close()
	tcp, err := network.ListenTCPAddrPort(netip.MustParseAddrPort("10.200.0.3:8000"))
	if err != nil {
		t.Fatal(err)
	}
	defer tcp.Close()
	go func() {
		connection, err := tcp.Accept()
		if err == nil {
			defer connection.Close()
			io.Copy(connection, connection)
		}
	}()
	udp, err := network.ListenUDPAddrPort(netip.MustParseAddrPort("10.200.0.3:8001"))
	if err != nil {
		t.Fatal(err)
	}
	defer udp.Close()
	go func() {
		buffer := make([]byte, 128)
		count, address, err := udp.ReadFrom(buffer)
		if err == nil {
			udp.WriteTo(buffer[:count], address)
		}
	}()
	for _, transport := range []struct{ kind, address string }{{"tcp", "10.200.0.3:8000"}, {"udp", "10.200.0.3:8001"}} {
		ctx, cancel := context.WithTimeout(context.Background(), 8*time.Second)
		connection, err := session.DialContext(ctx, transport.kind, transport.address)
		cancel()
		if err != nil {
			t.Fatal(transport.kind, err)
		}
		connection.SetDeadline(time.Now().Add(8 * time.Second))
		payload := []byte("Hop encrypted session " + transport.kind)
		if _, err := connection.Write(payload); err != nil {
			connection.Close()
			t.Fatal(err)
		}
		actual := make([]byte, len(payload))
		_, err = io.ReadFull(connection, actual)
		connection.Close()
		if err != nil || !bytes.Equal(actual, payload) {
			t.Fatalf("%s echo: %q %v", transport.kind, actual, err)
		}
	}
	if !strings.Contains(session.Statistics(), "rx_bytes=") {
		t.Fatal("missing statistics")
	}
}

func TestSessionRejectsUnprotectedSocket(t *testing.T) {
	session, err := OpenSession(syntheticProfile(), func(int) bool { return false })
	if session != nil {
		session.Close()
		t.Fatal("unprotected session started")
	}
	if err == nil {
		t.Fatal("missing protection failure")
	}
}
