package main

import (
	"context"
	"errors"
	xnet "github.com/xtls/xray-core/common/net"
	"github.com/xtls/xray-core/common/session"
	"github.com/xtls/xray-core/transport/internet"
	"net"
	"sync"
)

var errDirectDenied = errors.New("Direct endpoint permission denied")

type directDialer struct {
	base internet.SystemDialer
	open func(source, destination string, port uint16, protocol uint8) (func(), error)
}

func (d *directDialer) DestIpAddress() xnet.IP { return d.base.DestIpAddress() }
func (d *directDialer) Dial(ctx context.Context, source xnet.Address, destination xnet.Destination, options *internet.SocketConfig) (net.Conn, error) {
	outbounds := session.OutboundsFromContext(ctx)
	if len(outbounds) == 0 || outbounds[len(outbounds)-1].Tag != "hop-direct" {
		return d.base.Dial(ctx, source, destination, options)
	}
	// Direct разрешается только после разрешения домена и выбора физического исходного адреса.
	if source == nil || !source.Family().IsIP() || !destination.Address.Family().IsIP() {
		return nil, errDirectDenied
	}
	protocol := uint8(6)
	if destination.Network == xnet.Network_UDP {
		protocol = 17
	} else if destination.Network != xnet.Network_TCP {
		return nil, errDirectDenied
	}
	release, err := d.open(source.IP().String(), destination.Address.IP().String(), destination.Port.Value(), protocol)
	if err != nil {
		return nil, err
	}
	socket, err := d.base.Dial(ctx, source, destination, options)
	if err != nil {
		release()
		return nil, err
	}
	if packet, ok := socket.(*internet.PacketConnWrapper); ok {
		return &internet.PacketConnWrapper{PacketConn: &directPacketConn{PacketConn: packet.PacketConn, target: packet.Dest.String(), release: release}, Dest: packet.Dest}, nil
	}
	return &directConn{Conn: socket, release: release}, nil
}

type directConn struct {
	net.Conn
	once    sync.Once
	release func()
}

func (c *directConn) Close() error { err := c.Conn.Close(); c.once.Do(c.release); return err }

type directPacketConn struct {
	net.PacketConn
	target  string
	once    sync.Once
	release func()
}

func (c *directPacketConn) Close() error {
	err := c.PacketConn.Close()
	c.once.Do(c.release)
	return err
}
func (c *directPacketConn) WriteTo(bytes []byte, target net.Addr) (int, error) {
	// Один UDP-сокет не расширяет разрешение на чужой адрес при мультиплексировании.
	if target.String() != c.target {
		return 0, errDirectDenied
	}
	return c.PacketConn.WriteTo(bytes, target)
}
func (c *directPacketConn) ReadFrom(bytes []byte) (int, net.Addr, error) {
	for {
		size, source, err := c.PacketConn.ReadFrom(bytes)
		if err != nil || source.String() == c.target {
			return size, source, err
		}
	}
}
