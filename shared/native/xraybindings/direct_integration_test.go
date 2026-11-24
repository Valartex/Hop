package main

import (
	"bytes"
	"context"
	"crypto/sha256"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"github.com/xtls/xray-core/common"
	"github.com/xtls/xray-core/common/buf"
	xnet "github.com/xtls/xray-core/common/net"
	"github.com/xtls/xray-core/common/session"
	"github.com/xtls/xray-core/core"
	"github.com/xtls/xray-core/features/dns"
	"github.com/xtls/xray-core/features/routing"
	"github.com/xtls/xray-core/transport/internet"
)

// Реальное ядро получает UDP с FakeDNS-адресом, как от TUN; сетевой стенд ограничен loopback.
func TestDirectFakeDNSDatagramReachesResolvedEndpoint(t *testing.T) {
	echo, err := net.ListenPacket("udp4", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer echo.Close()
	go func() {
		packet := make([]byte, 1024)
		for {
			n, source, err := echo.ReadFrom(packet)
			if err != nil {
				return
			}
			_, _ = echo.WriteTo(packet[:n], source)
		}
	}()
	internet.UseAlternativeSystemDialer(&directDialer{base: &internet.DefaultSystemDialer{}, open: func(source, destination string, port uint16, protocol uint8) (func(), error) {
		if source != "127.0.0.1" || destination != "127.0.0.1" || int(port) != echo.LocalAddr().(*net.UDPAddr).Port || protocol != 17 {
			return nil, fmt.Errorf("unexpected Direct endpoint %s -> %s:%d/%d", source, destination, port, protocol)
		}
		return func() {}, nil
	}})
	defer internet.UseAlternativeSystemDialer(&directDialer{base: &internet.DefaultSystemDialer{}, open: openDirect})
	config := []byte(`{
		"log":{"loglevel":"none"},
		"dns":{"hosts":{"direct.hop.test":"127.0.0.1"},"servers":["fakedns"],"queryStrategy":"UseIPv4"},
		"fakedns":{"ipPool":"198.18.0.0/15","poolSize":256},
		"outbounds":[{"tag":"hop-vpn","protocol":"blackhole"},
		 {"tag":"hop-direct","protocol":"freedom","sendThrough":"127.0.0.1","settings":{"domainStrategy":"ForceIPv4"}}],
		"routing":{"domainStrategy":"IPOnDemand","rules":[{"type":"field","domain":["domain:direct.hop.test"],"outboundTag":"hop-direct"}]}
	}`)
	loaded, err := core.LoadConfig("json", bytes.NewReader(config))
	if err != nil {
		t.Fatal(err)
	}
	engine, err := core.New(loaded)
	if err != nil {
		t.Fatal(err)
	}
	defer engine.Close()
	if err = engine.Start(); err != nil {
		t.Fatal(err)
	}
	fakeDNS := engine.GetFeature((*dns.FakeDNSEngine)(nil)).(dns.FakeDNSEngine)
	fake := fakeDNS.GetFakeIPForDomain("direct.hop.test")[0]
	target := xnet.UDPDestination(fake, xnet.Port(echo.LocalAddr().(*net.UDPAddr).Port))
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	ctx = context.WithValue(ctx, core.XrayKey(1), engine)
	ctx = session.ContextWithContent(ctx, &session.Content{SniffingRequest: session.SniffingRequest{
		Enabled: true, OverrideDestinationForProtocol: []string{"fakedns", "http", "tls", "quic"},
	}})
	link, err := engine.GetFeature(routing.DispatcherType()).(routing.Dispatcher).Dispatch(ctx, target)
	if err != nil {
		t.Fatal(err)
	}
	defer common.Close(link.Writer)
	defer common.Interrupt(link.Reader)
	for sequence := range 3 {
		message := fmt.Sprintf("direct-fakedns-echo-%d", sequence)
		packet := buf.FromBytes([]byte(message))
		packetTarget := xnet.UDPDestination(fake, xnet.Port(echo.LocalAddr().(*net.UDPAddr).Port))
		packet.UDP = &packetTarget
		if err = link.Writer.WriteMultiBuffer(buf.MultiBuffer{packet}); err != nil {
			t.Fatal(err)
		}
		response := make(chan error, 1)
		go func() {
			packets, err := link.Reader.ReadMultiBuffer()
			defer buf.ReleaseMulti(packets)
			if err == nil && (len(packets) != 1 || string(packets[0].Bytes()) != message) {
				err = fmt.Errorf("unexpected echo response")
			}
			response <- err
		}()
		select {
		case err = <-response:
			if err != nil {
				t.Fatalf("datagram %d: %v", sequence, err)
			}
		case <-ctx.Done():
			t.Fatalf("Direct FakeDNS UDP datagram %d timed out", sequence)
		}
	}
}

// Крупные параллельные HTTPS-ответы проверяют тот же Direct-путь, что и изображения CDN.
func TestDirectFakeDNSConcurrentHTTPSImages(t *testing.T) {
	image := append([]byte("RIFF\x00\x00\x10\x00WEBP"), bytes.Repeat([]byte{31, 73, 155, 201}, 256*1024)...)
	wanted := sha256.Sum256(image)
	https := httptest.NewUnstartedServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "image/webp")
		_, _ = w.Write(image)
	}))
	https.EnableHTTP2 = true
	https.StartTLS()
	defer https.Close()
	port := https.Listener.Addr().(*net.TCPAddr).Port
	var opened, closed atomic.Int32
	internet.UseAlternativeSystemDialer(&directDialer{base: &internet.DefaultSystemDialer{}, open: func(source, destination string, destinationPort uint16, protocol uint8) (func(), error) {
		if source != "127.0.0.1" || destination != "127.0.0.1" || int(destinationPort) != port || protocol != 6 {
			return nil, errDirectDenied
		}
		opened.Add(1)
		return func() { closed.Add(1) }, nil
	}})
	defer internet.UseAlternativeSystemDialer(&directDialer{base: &internet.DefaultSystemDialer{}, open: openDirect})
	config := []byte(`{
		"log":{"loglevel":"none"},
		"dns":{"hosts":{"example.com":"127.0.0.1"},"servers":["fakedns"],"queryStrategy":"UseIPv4"},
		"fakedns":{"ipPool":"198.18.0.0/15","poolSize":256},
		"outbounds":[{"tag":"hop-vpn","protocol":"blackhole"},
		 {"tag":"hop-direct","protocol":"freedom","sendThrough":"127.0.0.1","settings":{"domainStrategy":"ForceIPv4"}}],
		"routing":{"domainStrategy":"IPOnDemand","rules":[{"type":"field","domain":["domain:example.com"],"outboundTag":"hop-direct"}]}
	}`)
	engine, err := core.StartInstance("json", config)
	if err != nil {
		t.Fatal(err)
	}
	defer engine.Close()
	fake := engine.GetFeature((*dns.FakeDNSEngine)(nil)).(dns.FakeDNSEngine).GetFakeIPForDomain("example.com")[0]
	transport := https.Client().Transport.(*http.Transport).Clone()
	transport.ForceAttemptHTTP2 = true
	transport.DialContext = func(ctx context.Context, _, _ string) (net.Conn, error) {
		ctx = session.ContextWithContent(ctx, &session.Content{SniffingRequest: session.SniffingRequest{
			Enabled: true, OverrideDestinationForProtocol: []string{"fakedns", "http", "tls", "quic"},
		}})
		return core.Dial(ctx, engine, xnet.TCPDestination(fake, xnet.Port(port)))
	}
	defer transport.CloseIdleConnections()
	client := &http.Client{Transport: transport, Timeout: 10 * time.Second}
	var requests sync.WaitGroup
	for range 32 {
		requests.Go(func() {
			response, err := client.Get(fmt.Sprintf("https://example.com:%d/image.webp", port))
			if err != nil {
				t.Error(err)
				return
			}
			defer response.Body.Close()
			body, err := io.ReadAll(response.Body)
			if err != nil || response.StatusCode != 200 || sha256.Sum256(body) != wanted {
				t.Errorf("incomplete HTTPS image: bytes=%d status=%d error=%v", len(body), response.StatusCode, err)
			}
		})
	}
	requests.Wait()
	transport.CloseIdleConnections()
	engine.Close()
	deadline := time.Now().Add(time.Second)
	for closed.Load() != opened.Load() && time.Now().Before(deadline) {
		time.Sleep(time.Millisecond)
	}
	if opened.Load() == 0 || closed.Load() != opened.Load() {
		t.Fatalf("Direct leases were not balanced: opened=%d closed=%d", opened.Load(), closed.Load())
	}
}
