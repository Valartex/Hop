package main

import (
	"bufio"
	"context"
	"crypto/rand"
	"encoding/base64"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"net/netip"
	"os"
	"strconv"
	"strings"
	"sync/atomic"
	"syscall"
	"time"

	"github.com/amnezia-vpn/amneziawg-go/v3/conn"
	"github.com/amnezia-vpn/amneziawg-go/v3/device"
	"github.com/amnezia-vpn/amneziawg-go/v3/tun/netstack"
	"golang.org/x/net/dns/dnsmessage"
)

type profile struct {
	addresses []netip.Addr
	dns       netip.Addr
	endpoint  string
	uapi      string
	mtu       int
}

// Разбираются только данные профиля. Системные команды из wg-quick никогда не выполняются.
func parseProfile(text string) (profile, error) {
	p := profile{mtu: 1280}
	fields := map[string]string{}
	section, peers := "", 0
	scanner := bufio.NewScanner(strings.NewReader(text))
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" || strings.HasPrefix(line, "#") || strings.HasPrefix(line, ";") {
			continue
		}
		if strings.HasPrefix(line, "[") {
			section = strings.ToLower(line)
			if section == "[peer]" {
				peers++
			} else if section != "[interface]" {
				return p, errors.New("section")
			}
			continue
		}
		key, value, found := strings.Cut(line, "=")
		key, value = strings.ToLower(strings.TrimSpace(key)), strings.TrimSpace(value)
		if !found || section == "" || value == "" {
			return p, errors.New("field")
		}
		if key == "address" || key == "dns" || key == "allowedips" {
			if fields[key] != "" {
				fields[key] += ","
			}
			fields[key] += value
		} else {
			if _, exists := fields[key]; exists {
				return p, errors.New("duplicate")
			}
			fields[key] = value
		}
	}
	if scanner.Err() != nil || peers != 1 {
		return p, errors.New("peer count")
	}
	if err := safeProbeParameters(fields); err != nil {
		return p, err
	}
	for _, value := range strings.Split(fields["address"], ",") {
		prefix, err := netip.ParsePrefix(strings.TrimSpace(value))
		if err != nil {
			return p, errors.New("address")
		}
		if prefix.Addr().IsUnspecified() || prefix.Addr().IsMulticast() {
			return p, errors.New("address")
		}
		p.addresses = append(p.addresses, prefix.Addr())
		if len(p.addresses) > 8 {
			return p, errors.New("address count")
		}
	}
	var allowed []netip.Prefix
	for _, value := range strings.Split(fields["allowedips"], ",") {
		prefix, err := netip.ParsePrefix(strings.TrimSpace(value))
		if err != nil {
			return p, errors.New("allowed IP")
		}
		allowed = append(allowed, prefix)
	}
	for _, value := range strings.Split(fields["dns"], ",") {
		address, err := netip.ParseAddr(strings.TrimSpace(value))
		if err != nil || !address.Is4() || address.IsLoopback() || address.IsUnspecified() {
			continue
		}
		for _, prefix := range allowed {
			if prefix.Contains(address) {
				p.dns = address
				break
			}
		}
		if p.dns.IsValid() {
			break
		}
	}
	if !p.dns.IsValid() {
		return p, errors.New("DNS outside tunnel")
	}
	if value := fields["mtu"]; value != "" {
		mtu, err := strconv.Atoi(value)
		if err != nil || mtu < 576 || mtu > 9000 {
			return p, errors.New("MTU")
		}
		p.mtu = mtu
	}
	keyHex := func(name string) (string, error) {
		key, err := base64.StdEncoding.Strict().DecodeString(fields[name])
		if err != nil || len(key) != 32 {
			return "", errors.New("key")
		}
		return hex.EncodeToString(key), nil
	}
	private, err := keyHex("privatekey")
	if err != nil {
		return p, err
	}
	public, err := keyHex("publickey")
	if err != nil {
		return p, err
	}
	var config strings.Builder
	fmt.Fprintf(&config, "private_key=%s\nlisten_port=0\n", private)
	parameters := map[string]string{"jc": "jc", "jmin": "jmin", "jmax": "jmax", "s1": "s1", "s2": "s2", "s3": "s3", "s4": "s4",
		"h1": "h1", "h2": "h2", "h3": "h3", "h4": "h4", "i1": "i1", "i2": "i2", "i3": "i3", "i4": "i4", "i5": "i5",
		"contentpaddingaddition": "content_padding_addition", "rekeyaftertime": "rekey_after_time", "rekeytimeout": "rekey_timeout",
		"rejectaftertime": "reject_after_time", "keepalivetimeout": "keepalive_timeout", "maxhandshakeattempts": "max_handshake_attempts",
		"randomtrailers": "random_trailers", "disablecookies": "disable_cookies"}
	for name, uapi := range parameters {
		if value := fields[name]; value != "" {
			fmt.Fprintf(&config, "%s=%s\n", uapi, value)
		}
	}
	if fields["headerprotectionkey"] != "" {
		key, err := keyHex("headerprotectionkey")
		if err != nil {
			return p, err
		}
		fmt.Fprintf(&config, "header_protection_key=%s\n", key)
	}
	fmt.Fprintf(&config, "public_key=%s\n", public)
	if fields["presharedkey"] != "" {
		key, err := keyHex("presharedkey")
		if err != nil {
			return p, err
		}
		fmt.Fprintf(&config, "preshared_key=%s\n", key)
	}
	// Для измерения нужен только один DNS-путь, а не полная таблица маршрутов профиля.
	fmt.Fprintf(&config, "allowed_ip=%s/32\n", p.dns)
	p.endpoint = fields["endpoint"]
	if _, _, err := net.SplitHostPort(p.endpoint); err != nil {
		return p, errors.New("endpoint")
	}
	p.uapi = config.String()
	return p, nil
}

// Диагностический процесс не принимает параметры, способные создать неограниченный поток/буфер.
func safeProbeParameters(fields map[string]string) error {
	for name, maximum := range map[string]uint64{"jc": 128, "jmin": 2000, "jmax": 2000, "s1": 1500, "s2": 1500, "s3": 1500, "s4": 1500} {
		if value := fields[name]; value != "" {
			number, err := strconv.ParseUint(value, 10, 32)
			if err != nil || number > maximum {
				return errors.New("probe resource limit")
			}
		}
	}
	if fields["jc"] != "" && fields["jc"] != "0" {
		minimum, err1 := strconv.ParseUint(fields["jmin"], 10, 32)
		maximum, err2 := strconv.ParseUint(fields["jmax"], 10, 32)
		if err1 != nil || err2 != nil || minimum > maximum {
			return errors.New("junk range")
		}
	}
	if value := fields["contentpaddingaddition"]; value != "" {
		for _, bound := range strings.Split(value, "-") {
			number, err := strconv.ParseUint(bound, 10, 32)
			if err != nil || number > 128 {
				return errors.New("padding limit")
			}
		}
	}
	for _, name := range []string{"i1", "i2", "i3", "i4", "i5"} {
		remaining, total, tags := fields[name], 0, 0
		for strings.TrimSpace(remaining) != "" {
			remaining = strings.TrimSpace(remaining)
			end := strings.IndexByte(remaining, '>')
			if !strings.HasPrefix(remaining, "<") || end < 0 {
				return errors.New("CPS")
			}
			parts := strings.Fields(remaining[1:end])
			if len(parts) == 0 {
				return errors.New("CPS")
			}
			switch parts[0] {
			case "r", "rc", "rd":
				if len(parts) != 2 {
					return errors.New("CPS")
				}
				number, err := strconv.Atoi(parts[1])
				if err != nil || number < 0 || number > 2000 {
					return errors.New("CPS limit")
				}
				total += number
			case "b":
				if len(parts) != 2 {
					return errors.New("CPS")
				}
				literal, err := hex.DecodeString(strings.TrimPrefix(parts[1], "0x"))
				if err != nil {
					return errors.New("CPS")
				}
				total += len(literal)
			case "t":
				total += 8
			default:
				return errors.New("CPS unavailable for probe")
			}
			tags++
			if tags > 64 || total > 2000 {
				return errors.New("CPS limit")
			}
			remaining = remaining[end+1:]
		}
	}
	return nil
}

// NetTUN — только пользовательский сетевой стек Go; адаптеры, службы и маршруты ОС не создаются.
func probe(ctx context.Context, p profile, statistics map[string]any) (int64, error) {
	host, portText, err := net.SplitHostPort(p.endpoint)
	if err != nil {
		return -1, err
	}
	port, err := strconv.Atoi(portText)
	if err != nil || port < 1 || port > 65535 {
		return -1, errors.New("port")
	}
	addresses, err := net.DefaultResolver.LookupNetIP(ctx, "ip", host)
	if err != nil || len(addresses) == 0 {
		return -1, errors.New("resolve")
	}
	// Resolver может вернуть IPv4 в форме ::ffff:x.x.x.x; Windows bind должен выбрать IPv4-сокет.
	endpoint := netip.AddrPortFrom(addresses[0].Unmap(), uint16(port))
	if endpoint.Addr().IsUnspecified() || endpoint.Addr().IsMulticast() || endpoint.Addr().Zone() != "" {
		return -1, errors.New("endpoint")
	}
	tun, network, err := netstack.CreateNetTUN(p.addresses, nil, p.mtu)
	if err != nil {
		return -1, errors.New("netstack")
	}
	var transportErrors, transportCode atomic.Int64
	logger := device.NewLogger(device.LogLevelSilent, "")
	logger.Errorf = func(_ string, arguments ...any) {
		transportErrors.Add(1)
		for _, argument := range arguments {
			if failure, ok := argument.(error); ok {
				var code syscall.Errno
				if errors.As(failure, &code) {
					transportCode.Store(int64(code))
				}
			}
		}
	}
	peer := device.NewDevice(tun, conn.NewDefaultBind(), logger)
	defer peer.Close()
	defer func() {
		statistics["transportErrors"] = transportErrors.Load()
		statistics["transportCode"] = transportCode.Load()
		if state, err := peer.IpcGet(); err == nil {
			for _, line := range strings.Split(state, "\n") {
				name, value, found := strings.Cut(line, "=")
				if found && (name == "rx_bytes" || name == "tx_bytes" || name == "last_handshake_time_sec") {
					if number, err := strconv.ParseUint(value, 10, 64); err == nil {
						statistics[name] = number
					}
				}
			}
		}
	}()
	if peer.IpcSet(p.uapi+"endpoint="+endpoint.String()+"\n") != nil {
		return -1, errors.New("configure")
	}
	if peer.Up() != nil {
		return -1, errors.New("start")
	}
	connection, err := network.DialUDP(nil, &net.UDPAddr{IP: net.IP(p.dns.AsSlice()), Port: 53})
	if err != nil {
		return -1, errors.New("DNS socket")
	}
	defer connection.Close()
	if deadline, ok := ctx.Deadline(); ok {
		connection.SetDeadline(deadline)
	}
	var random [8]byte
	if _, err := rand.Read(random[:]); err != nil {
		return -1, err
	}
	name := dnsmessage.MustNewName("h" + hex.EncodeToString(random[2:]) + ".example.com.")
	question := dnsmessage.Question{Name: name, Type: dnsmessage.TypeA, Class: dnsmessage.ClassINET}
	query := dnsmessage.Message{Header: dnsmessage.Header{ID: binary.BigEndian.Uint16(random[:2]), RecursionDesired: true}, Questions: []dnsmessage.Question{question}}
	request, err := query.Pack()
	if err != nil {
		return -1, err
	}
	started := time.Now()
	if _, err := connection.Write(request); err != nil {
		return -1, errors.New("send")
	}
	response := make([]byte, 4096)
	attempts := 1
	finalDeadline, hasDeadline := ctx.Deadline()
	for {
		readDeadline := finalDeadline
		if attempts < 3 && (!hasDeadline || time.Now().Add(time.Second).Before(finalDeadline)) {
			readDeadline = time.Now().Add(time.Second)
		}
		connection.SetReadDeadline(readDeadline)
		size, err := connection.Read(response)
		if err != nil {
			var failure net.Error
			if errors.As(err, &failure) && failure.Timeout() && attempts < 3 && ctx.Err() == nil {
				// Первый UDP-запрос может потеряться во время handshake; всего не более трёх отправок.
				attempts++
				if _, err := connection.Write(request); err != nil {
					return -1, errors.New("send")
				}
				continue
			}
			return -1, errors.New("reply")
		}
		if validDNSResponse(response[:size], query) {
			return time.Since(started).Milliseconds(), nil
		}
	}
}

func validDNSResponse(response []byte, query dnsmessage.Message) bool {
	var message dnsmessage.Message
	return message.Unpack(response) == nil && message.ID == query.ID && message.Response && !message.Truncated && len(message.Questions) == 1 && len(query.Questions) == 1 && message.Questions[0] == query.Questions[0] && (message.RCode == dnsmessage.RCodeSuccess || message.RCode == dnsmessage.RCodeNameError)
}

func run(reader io.Reader, writer io.Writer) int {
	data, err := io.ReadAll(io.LimitReader(reader, 65537))
	var input struct {
		Config string `json:"config"`
	}
	if err != nil || len(data) > 65536 || json.Unmarshal(data, &input) != nil {
		return 2
	}
	defer clear(data)
	p, err := parseProfile(input.Config)
	if err != nil {
		json.NewEncoder(writer).Encode(map[string]any{"ok": false, "unsupported": true, "latencyMs": -1})
		return 4
	}
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	statistics := map[string]any{}
	latency, err := probe(ctx, p, statistics)
	reason := ""
	if err != nil {
		reason = "internal"
		for _, stage := range []string{"port", "resolve", "endpoint", "netstack", "configure", "start", "DNS socket", "send", "reply"} {
			if err.Error() == stage {
				reason = stage
				break
			}
		}
	}
	json.NewEncoder(writer).Encode(map[string]any{"ok": err == nil, "latencyMs": latency, "reason": reason, "statistics": statistics})
	if err != nil {
		return 3
	}
	return 0
}

func main() {
	// Профиль поступает исключительно в stdin. Ошибки и ключи не попадают в журналы/argv.
	os.Exit(run(bufio.NewReader(os.Stdin), os.Stdout))
}
