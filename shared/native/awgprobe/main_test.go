package awgprobe

import (
	"bytes"
	"encoding/base64"
	"strings"
	"testing"

	"golang.org/x/net/dns/dnsmessage"
)

func syntheticProfile() string {
	key := base64.StdEncoding.EncodeToString(bytes.Repeat([]byte{1}, 32))
	return "[Interface]\nPrivateKey = " + key + "\nAddress = 10.77.0.2/32\nDNS = 10.77.0.1\nJc = 2\nJmin = 40\nJmax = 128\nS3 = 64\nHeaderProtectionKey = " + key + "\nRandomTrailers = true\n[Peer]\nPublicKey = " + key + "\nAllowedIPs = 10.77.0.1/32\nEndpoint = 192.0.2.1:51820\n"
}

func TestProfileAWG31AndNoSystemCommands(t *testing.T) {
	p, err := parseProfile(syntheticProfile() + "PostUp = echo forbidden\n")
	if err != nil {
		t.Fatal(err)
	}
	if p.dns.String() != "10.77.0.1" || len(p.addresses) != 1 || !strings.Contains(p.uapi, "s3=64\n") || !strings.Contains(p.uapi, "header_protection_key=") || strings.Contains(p.uapi, "echo") || strings.Contains(p.uapi, "PostUp") {
		t.Fatal("incorrect conversion")
	}
}

func TestRejectAmbiguousOrUnroutableProfiles(t *testing.T) {
	for _, input := range []string{
		strings.Replace(syntheticProfile(), "DNS = 10.77.0.1", "DNS = 1.1.1.1", 1),
		syntheticProfile() + "[Peer]\nPublicKey = invalid\n",
		strings.Replace(syntheticProfile(), "Address = 10.77.0.2/32", "Address = invalid", 1),
		strings.Replace(syntheticProfile(), "Endpoint = 192.0.2.1:51820", "Endpoint = missing-port", 1),
		strings.Replace(syntheticProfile(), "Jc = 2", "Jc = 4294967295", 1),
		strings.Replace(syntheticProfile(), "Jmax = 128", "Jmax = 4294967295", 1),
		syntheticProfile() + "I1 = <r 1000000000>\n",
		syntheticProfile() + "I1 = <r 1500><rc 1500>\n",
	} {
		if _, err := parseProfile(input); err == nil {
			t.Fatal("accepted invalid profile")
		}
	}
}

func TestUnsupportedNeverEchoesInput(t *testing.T) {
	var output bytes.Buffer
	if code := run(strings.NewReader(`{"config":"private synthetic secret"}`), &output); code != 4 || strings.Contains(output.String(), "secret") || !strings.Contains(output.String(), `"unsupported":true`) {
		t.Fatal("unsafe unsupported result")
	}
	output.Reset()
	if run(strings.NewReader(strings.Repeat("x", 65537)), &output) != 2 || output.Len() != 0 {
		t.Fatal("unbounded input")
	}
}

func TestDNSResponseMustMatchEncryptedProbe(t *testing.T) {
	question := dnsmessage.Question{Name: dnsmessage.MustNewName("nonce.example.com."), Type: dnsmessage.TypeA, Class: dnsmessage.ClassINET}
	query := dnsmessage.Message{Header: dnsmessage.Header{ID: 123}, Questions: []dnsmessage.Question{question}}
	response := dnsmessage.Message{Header: dnsmessage.Header{ID: 123, Response: true, RCode: dnsmessage.RCodeNameError}, Questions: []dnsmessage.Question{question}}
	pack := func(message dnsmessage.Message) []byte {
		data, err := message.Pack()
		if err != nil {
			t.Fatal(err)
		}
		return data
	}
	if !validDNSResponse(pack(response), query) {
		t.Fatal("rejected valid DNS response")
	}
	for _, change := range []func(*dnsmessage.Message){
		func(m *dnsmessage.Message) { m.ID++ },
		func(m *dnsmessage.Message) { m.Response = false },
		func(m *dnsmessage.Message) { m.Truncated = true },
		func(m *dnsmessage.Message) { m.RCode = dnsmessage.RCodeServerFailure },
		func(m *dnsmessage.Message) {
			m.Questions = []dnsmessage.Question{{Name: dnsmessage.MustNewName("other.example.com."), Type: dnsmessage.TypeA, Class: dnsmessage.ClassINET}}
		},
	} {
		bad := response
		change(&bad)
		if validDNSResponse(pack(bad), query) {
			t.Fatal("accepted unrelated response")
		}
	}
}
