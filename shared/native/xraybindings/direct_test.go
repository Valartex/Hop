package main

import (
    "context"
    "errors"
    "net"
    "testing"
    "time"
    "github.com/xtls/xray-core/common/session"
    xnet "github.com/xtls/xray-core/common/net"
    "github.com/xtls/xray-core/transport/internet"
)
type mockConn struct { closed int }
func (c *mockConn) Read([]byte)(int,error){return 0,net.ErrClosed}
func (c *mockConn) Write(b []byte)(int,error){return len(b),nil}
func (c *mockConn) Close()error{c.closed++;return nil}
func (c *mockConn) LocalAddr()net.Addr{return &net.TCPAddr{IP:net.IPv4(10,0,2,15),Port:12345}}
func (c *mockConn) RemoteAddr()net.Addr{return &net.TCPAddr{IP:net.IPv4(192,0,2,1),Port:443}}
func (*mockConn) SetDeadline(time.Time)error{return nil}
func (*mockConn) SetReadDeadline(time.Time)error{return nil}
func (*mockConn) SetWriteDeadline(time.Time)error{return nil}
type mockDialer struct { calls int; result net.Conn; err error }
func (d *mockDialer) DestIpAddress()xnet.IP{return nil}
func (d *mockDialer) Dial(context.Context,xnet.Address,xnet.Destination,*internet.SocketConfig)(net.Conn,error){d.calls++;return d.result,d.err}
func directContext(tag string)context.Context{return session.ContextWithOutbounds(context.Background(),[]*session.Outbound{{Tag:tag}})}
func TestDirectLeaseLifetime(t *testing.T){
    base:=&mockDialer{result:&mockConn{}}
    opened,closed:=0,0
    d:=&directDialer{base:base,open:func(source,destination string,port uint16,protocol uint8)(func(),error){
        if source!="10.0.2.15"||destination!="192.0.2.1"||port!=443||protocol!=6{t.Fatal("endpoint widened")}
        opened++;return func(){closed++},nil
    }}
    socket,err:=d.Dial(directContext("hop-direct"),xnet.ParseAddress("10.0.2.15"),xnet.TCPDestination(xnet.ParseAddress("192.0.2.1"),443),nil)
    if err!=nil||opened!=1||closed!=0{t.Fatal("lease not held",err,opened,closed)}
    socket.Close();socket.Close();if closed!=1{t.Fatal("lease close not idempotent",closed)}
}
func TestDeniedDirectDoesNotDial(t *testing.T){
    base:=&mockDialer{}
    d:=&directDialer{base:base,open:func(string,string,uint16,uint8)(func(),error){return nil,errDirectDenied}}
    _,err:=d.Dial(directContext("hop-direct"),xnet.ParseAddress("10.0.2.15"),xnet.TCPDestination(xnet.ParseAddress("192.0.2.1"),443),nil)
    if err==nil||base.calls!=0{t.Fatal("denied Direct reached network")}
    _,err=d.Dial(directContext("hop-direct"),nil,xnet.TCPDestination(xnet.ParseAddress("example.com"),443),nil)
    if err==nil||base.calls!=0{t.Fatal("unresolved Direct reached network")}
}
func TestFailedDialReleasesAndVpnDoesNotLease(t *testing.T){
    base:=&mockDialer{err:errors.New("dial failed")};closed:=0
    d:=&directDialer{base:base,open:func(string,string,uint16,uint8)(func(),error){return func(){closed++},nil}}
    d.Dial(directContext("hop-direct"),xnet.ParseAddress("10.0.2.15"),xnet.TCPDestination(xnet.ParseAddress("192.0.2.1"),443),nil)
    if closed!=1{t.Fatal("failed dial left permit")}
    d.Dial(directContext("hop-vpn"),nil,xnet.TCPDestination(xnet.ParseAddress("192.0.2.2"),443),nil)
    if base.calls!=2||closed!=1{t.Fatal("VPN requested Direct lease")}
}
type mockPacketConn struct {writes int;closed int}
func (*mockPacketConn) ReadFrom([]byte)(int,net.Addr,error){return 0,nil,net.ErrClosed}
func (c *mockPacketConn) WriteTo(b []byte,_ net.Addr)(int,error){c.writes++;return len(b),nil}
func (c *mockPacketConn) Close()error{c.closed++;return nil}
func (*mockPacketConn) LocalAddr()net.Addr{return &net.UDPAddr{IP:net.IPv4(10,0,2,15),Port:12345}}
func (*mockPacketConn) SetDeadline(time.Time)error{return nil}
func (*mockPacketConn) SetReadDeadline(time.Time)error{return nil}
func (*mockPacketConn) SetWriteDeadline(time.Time)error{return nil}
func TestUdpCannotExpandEndpoint(t *testing.T){
    packet:=&mockPacketConn{};closed:=0
    c:=&directPacketConn{PacketConn:packet,target:"192.0.2.1:443",release:func(){closed++}}
    if _,err:=c.WriteTo([]byte("x"),&net.UDPAddr{IP:net.IPv4(192,0,2,2),Port:443});err==nil||packet.writes!=0{t.Fatal("UDP permit widened")}
    if _,err:=c.WriteTo([]byte("x"),&net.UDPAddr{IP:net.IPv4(192,0,2,1),Port:443});err!=nil||packet.writes!=1{t.Fatal("exact UDP target rejected")}
    c.Close();c.Close();if closed!=1{t.Fatal("UDP lease not released once")}
}
