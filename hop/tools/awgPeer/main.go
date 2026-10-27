package main

import (
	"bytes"
	"crypto/ecdh"
	"crypto/rand"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"net"
	"net/http"
	"net/netip"
	"os"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/amnezia-vpn/amneziawg-go/v3/conn"
	"github.com/amnezia-vpn/amneziawg-go/v3/device"
	"github.com/amnezia-vpn/amneziawg-go/v3/tun/netstack"
)

// На хосте открываются только loopback UDP/TCP. TUN и маршруты хоста не создаются.
type loopbackBind struct {
	mutex         sync.Mutex
	socket        *net.UDPConn
	actualPort    uint16
	receivedBytes atomic.Int64
	sentBytes     atomic.Int64
}

func (b *loopbackBind) Open(port uint16) ([]conn.ReceiveFunc, uint16, error) {
	b.mutex.Lock()
	defer b.mutex.Unlock()
	if b.socket != nil {
		return nil, 0, conn.ErrBindAlreadyOpen
	}
	socket, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: int(port)})
	if err != nil {
		return nil, 0, err
	}
	// Размер как у штатного conn.StdNetBind: малый UDP-буфер искажает замер через NAT VM.
	if err := socket.SetReadBuffer(7 << 20); err != nil {
		socket.Close()
		return nil, 0, err
	}
	if err := socket.SetWriteBuffer(7 << 20); err != nil {
		socket.Close()
		return nil, 0, err
	}
	b.socket = socket
	b.actualPort = uint16(socket.LocalAddr().(*net.UDPAddr).Port)
	receive := func(packets [][]byte, sizes []int, endpoints []conn.Endpoint) (int, error) {
		size, address, err := socket.ReadFromUDPAddrPort(packets[0])
		if err != nil {
			return 0, err
		}
		sizes[0] = size
		b.receivedBytes.Add(int64(size))
		endpoints[0] = &conn.StdNetEndpoint{AddrPort: address}
		return 1, nil
	}
	return []conn.ReceiveFunc{receive}, b.actualPort, nil
}
func (b *loopbackBind) Close() error {
	b.mutex.Lock()
	defer b.mutex.Unlock()
	if b.socket == nil {
		return nil
	}
	err := b.socket.Close()
	b.socket = nil
	return err
}
func (b *loopbackBind) Send(packets [][]byte, endpoint conn.Endpoint) error {
	b.mutex.Lock()
	socket := b.socket
	b.mutex.Unlock()
	if socket == nil {
		return net.ErrClosed
	}
	target, ok := endpoint.(*conn.StdNetEndpoint)
	if !ok {
		return conn.ErrWrongEndpointType
	}
	for _, packet := range packets {
		if size, err := socket.WriteToUDPAddrPort(packet, target.AddrPort); err != nil {
			return err
		} else {
			b.sentBytes.Add(int64(size))
		}
	}
	return nil
}
func (b *loopbackBind) SetMark(uint32) error { return nil }
func (b *loopbackBind) BatchSize() int       { return 1 }
func (b *loopbackBind) ParseEndpoint(text string) (conn.Endpoint, error) {
	address, err := netip.ParseAddrPort(text)
	if err != nil || !address.Addr().Is4() {
		return nil, fmt.Errorf("IPv4 endpoint required")
	}
	return &conn.StdNetEndpoint{AddrPort: address}, nil
}

func randomKey() ([]byte, error) {
	value := make([]byte, 32)
	_, err := rand.Read(value)
	return value, err
}

func run() error {
	server, err := ecdh.X25519().GenerateKey(rand.Reader)
	if err != nil {
		return err
	}
	client, err := ecdh.X25519().GenerateKey(rand.Reader)
	if err != nil {
		return err
	}
	header, err := randomKey()
	if err != nil {
		return err
	}
	psk, err := randomKey()
	if err != nil {
		return err
	}
	tun, network, err := netstack.CreateNetTUN([]netip.Addr{netip.MustParseAddr("10.77.0.1")}, nil, 1280)
	if err != nil {
		return err
	}
	bind := &loopbackBind{}
	peer := device.NewDevice(tun, bind, device.NewLogger(device.LogLevelSilent, ""))
	defer peer.Close()
	var httpRequests, dnsRequests, echoRequests atomic.Int64
	var dnsPaused atomic.Bool
	var dropHealthQuery atomic.Bool
	go func() {
		for range time.Tick(5 * time.Second) {
			stats := map[string]int64{"http": httpRequests.Load(), "dns": dnsRequests.Load(), "echo": echoRequests.Load(), "udpReceived": bind.receivedBytes.Load(), "udpSent": bind.sentBytes.Load()}
			if text, err := peer.IpcGet(); err == nil {
				for _, line := range strings.Split(text, "\n") {
					name, value, found := strings.Cut(line, "=")
					if found && (name == "rx_bytes" || name == "tx_bytes") {
						stats[name], _ = strconv.ParseInt(value, 10, 64)
					}
				}
			}
			// Только числовые счётчики; ключи IpcGet никогда не выводятся.
			json.NewEncoder(os.Stderr).Encode(stats)
		}
	}()
	parameters := "jc=2\njmin=40\njmax=128\ns1=64\ns2=80\ns3=64\ns4=64\nh1=101001-101100\nh2=202001-202100\nh3=303001-303100\nh4=404001-404100\ncontent_padding_addition=0-16\nrandom_trailers=true\n"
	config := "private_key=" + hex.EncodeToString(server.Bytes()) + "\nlisten_port=0\n" + parameters
	config += "header_protection_key=" + hex.EncodeToString(header) + "\npublic_key=" + hex.EncodeToString(client.PublicKey().Bytes())
	config += "\npreshared_key=" + hex.EncodeToString(psk) + "\nallowed_ip=10.77.0.2/32\npersistent_keepalive_interval=5\n"
	if err := peer.IpcSet(config); err != nil {
		return fmt.Errorf("peer configuration rejected")
	}
	if err := peer.Up(); err != nil {
		return err
	}
	listener, err := network.ListenTCP(&net.TCPAddr{IP: net.IPv4(10, 77, 0, 1), Port: 8080})
	if err != nil {
		return err
	}
	defer listener.Close()
	serverHTTP := &http.Server{Handler: http.HandlerFunc(func(writer http.ResponseWriter, request *http.Request) {
		httpRequests.Add(1)
		if request.URL.Path == "/bytes-4m" {
			// Ограниченный поток с проверяемым содержимым, без внешнего speed-test сервиса.
			writer.Header().Set("Content-Length", "4194304")
			writer.Header().Set("Content-Type", "application/octet-stream")
			block := make([]byte, 65536)
			for index := range block {
				block[index] = byte(index)
			}
			for index := 0; index < 64; index++ {
				if _, err := writer.Write(block); err != nil {
					return
				}
			}
			return
		}
		if request.URL.Path != "/generate_204" {
			http.NotFound(writer, request)
			return
		}
		writer.WriteHeader(http.StatusNoContent)
	}), ReadHeaderTimeout: 5 * time.Second, IdleTimeout: 5 * time.Second}
	defer serverHTTP.Close()
	go serverHTTP.Serve(listener)
	directListener, err := net.Listen("tcp4", "127.0.0.1:0")
	if err != nil {
		return err
	}
	defer directListener.Close()
	directHTTP := &http.Server{Handler: http.HandlerFunc(func(writer http.ResponseWriter, request *http.Request) {
		if request.URL.Path == "/drop-next-health-dns" {
			dropHealthQuery.Store(true)
			writer.WriteHeader(http.StatusNoContent)
			return
		}
		if request.URL.Path == "/pause-dns" {
			dnsPaused.Store(true)
			writer.WriteHeader(http.StatusNoContent)
			return
		}
		serverHTTP.Handler.ServeHTTP(writer, request)
	}), ReadHeaderTimeout: 5 * time.Second, IdleTimeout: 30 * time.Second}
	defer directHTTP.Close()
	go directHTTP.Serve(directListener)

	udp, err := network.ListenUDP(&net.UDPAddr{IP: net.IPv4(10, 77, 0, 1), Port: 5353})
	if err != nil {
		return err
	}
	defer udp.Close()
	dns, err := network.ListenUDP(&net.UDPAddr{IP: net.IPv4(10, 77, 0, 1), Port: 53})
	if err != nil {
		return err
	}
	defer dns.Close()
	go func() {
		buffer := make([]byte, 512)
		for {
			size, address, err := dns.ReadFrom(buffer)
			if err != nil {
				return
			}
			if size < 17 || buffer[2]&0x80 != 0 || buffer[4] != 0 || buffer[5] != 1 {
				continue
			}
			// Достаточный ответ A для проверки пути; внешние DNS/маршруты хоста не используются.
			dnsRequests.Add(1)
			if bytes.Contains(buffer[12:size], []byte("\x0ahop-health\x07example\x03com")) && dropHealthQuery.CompareAndSwap(true, false) {
				continue
			}
			if dnsPaused.Load() {
				continue
			}
			response := append([]byte(nil), buffer[:size]...)
			response[2], response[3] = 0x81, 0x80
			response[6], response[7] = 0, 1
			response[8], response[9], response[10], response[11] = 0, 0, 0, 0
			answer := []byte{10, 77, 0, 1}
			if bytes.Contains(buffer[12:size], []byte("\x06direct\x03hop\x04test")) {
				answer = []byte{10, 0, 2, 2}
			}
			response = append(response, 0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0, 1, 0, 4)
			response = append(response, answer...)
			dns.WriteTo(response, address)
		}
	}()
	go func() {
		bytes := make([]byte, 4096)
		for {
			size, address, err := udp.ReadFrom(bytes)
			if err != nil {
				return
			}
			if string(bytes[:size]) == "hop-awg-echo" {
				echoRequests.Add(1)
				udp.WriteTo(bytes[:size], address)
			}
		}
	}()
	encode := base64.StdEncoding.EncodeToString
	clientConfig := fmt.Sprintf("[Interface]\nPrivateKey = %s\nAddress = 10.77.0.2/32\nDNS = 1.1.1.1\nMTU = 1280\nJc = 2\nJmin = 40\nJmax = 128\nS1 = 64\nS2 = 80\nS3 = 64\nS4 = 64\nH1 = 101001-101100\nH2 = 202001-202100\nH3 = 303001-303100\nH4 = 404001-404100\nHeaderProtectionKey = %s\nContentPaddingAddition = 0-16\nRandomTrailers = true\n[Peer]\nPublicKey = %s\nPresharedKey = %s\nAllowedIPs = 10.77.0.1/32\nEndpoint = 10.0.2.2:%d\nPersistentKeepalive = 5\n", encode(client.Bytes()), encode(header), encode(server.PublicKey().Bytes()), encode(psk), bind.actualPort)
	// Единственный вывод содержит конфигурацию; вызывающий скрипт читает её в память и шифрует для VM.
	if err := json.NewEncoder(os.Stdout).Encode(map[string]string{"config": clientConfig, "directPort": strconv.Itoa(directListener.Addr().(*net.TCPAddr).Port)}); err != nil {
		return err
	}
	time.Sleep(3 * time.Minute)
	return nil
}

func main() {
	if err := run(); err != nil {
		fmt.Fprintln(os.Stderr, "AWG fixture failed:", err)
		os.Exit(1)
	}
}
