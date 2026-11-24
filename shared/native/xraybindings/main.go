package main

/*
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifndef AMNEZIA_XRAY_DEF
#define AMNEZIA_XRAY_DEF

#ifdef _MSC_VER
// MSVC does not support complex type definitions
// So here is a workaround to bypass this behavior
// See go.dev/issues/36233
typedef float _Fcomplex;
typedef double _Dcomplex;
#endif

typedef void (*amnezia_xray_sockcallback)(uintptr_t fd, void* ctx);
typedef void (*amnezia_xray_loghandler)(char* str, void* ctx);
typedef uint64_t (*hop_xray_direct_open)(const char* source, const char* destination, uint16_t port, uint8_t protocol, void* ctx);
typedef void (*hop_xray_direct_close)(uint64_t lease, void* ctx);
static inline uint64_t hop_xray_opendirect(hop_xray_direct_open cb, const char* source, const char* destination, uint16_t port, uint8_t protocol, void* ctx) {
    return cb(source, destination, port, protocol, ctx);
}
static inline void hop_xray_closedirect(hop_xray_direct_close cb, uint64_t lease, void* ctx) { cb(lease, ctx); }

static inline void amnezia_xray_invokesockcallback(amnezia_xray_sockcallback cb, uintptr_t fd, void* ctx)
{
	cb(fd, ctx);
}

static inline void amnezia_xray_invokeloghandler(amnezia_xray_loghandler cb, char* msg, void* ctx)
{
	cb(msg, ctx);
}

#endif
*/
import "C"

import (
	"bytes"
	"sync"
	"syscall"
	"unsafe"

	"github.com/xtls/xray-core/app/log"
	cLog "github.com/xtls/xray-core/common/log"
	"github.com/xtls/xray-core/transport/internet"

	"github.com/xtls/xray-core/core"
	_ "github.com/xtls/xray-core/main/distro/all"
)

var server *core.Instance = nil
var callbacks struct {
	sync.RWMutex
	socket        C.amnezia_xray_sockcallback
	socketContext unsafe.Pointer
	open          C.hop_xray_direct_open
	close         C.hop_xray_direct_close
	directContext unsafe.Pointer
}

func init() {
	// Контроллер регистрируется один раз, смена соединения не накапливает callbacks.
	_ = internet.RegisterDialerController(func(network, address string, socket syscall.RawConn) error {
		callbacks.RLock()
		cb, ctx := callbacks.socket, callbacks.socketContext
		callbacks.RUnlock()
		if cb != nil {
			return socket.Control(func(fd uintptr) { C.amnezia_xray_invokesockcallback(cb, C.uintptr_t(fd), ctx) })
		}
		return nil
	})
	internet.UseAlternativeSystemDialer(&directDialer{base: &internet.DefaultSystemDialer{}, open: openDirect})
}

func openDirect(source, destination string, port uint16, protocol uint8) (func(), error) {
	callbacks.RLock()
	open, close, ctx := callbacks.open, callbacks.close, callbacks.directContext
	callbacks.RUnlock()
	if open == nil || close == nil {
		return nil, errDirectDenied
	}
	src, dst := C.CString(source), C.CString(destination)
	defer C.free(unsafe.Pointer(src))
	defer C.free(unsafe.Pointer(dst))
	lease := C.hop_xray_opendirect(open, src, dst, C.uint16_t(port), C.uint8_t(protocol), ctx)
	if lease == 0 {
		return nil, errDirectDenied
	}
	return func() { C.hop_xray_closedirect(close, lease, ctx) }, nil
}

//export hop_xray_setdirectcallbacks
func hop_xray_setdirectcallbacks(open C.hop_xray_direct_open, close C.hop_xray_direct_close, ctx unsafe.Pointer) {
	callbacks.Lock()
	callbacks.open, callbacks.close, callbacks.directContext = open, close, ctx
	callbacks.Unlock()
}

//export amnezia_xray_free
func amnezia_xray_free(ptr unsafe.Pointer) {
	C.free(ptr)
}

//export amnezia_xray_configure
func amnezia_xray_configure(cConfig *C.char) *C.char {
	strConfig := C.GoString(cConfig)
	cfgReader := bytes.NewReader([]byte(strConfig))

	coreConfig, err := core.LoadConfig("json", cfgReader)
	if err != nil {
		return C.CString(err.Error())
	}

	next, err := core.New(coreConfig)
	if err != nil {
		return C.CString(err.Error())
	}
	if server != nil {
		_ = server.Close()
	}
	server = next
	return nil
}

//export amnezia_xray_start
func amnezia_xray_start() *C.char {
	if server == nil {
		return nil
	}

	if err := server.Start(); err != nil {
		return C.CString(err.Error())
	}

	return nil
}

//export amnezia_xray_stop
func amnezia_xray_stop() *C.char {
	if server == nil {
		return nil
	}

	if err := server.Close(); err != nil {
		return C.CString(err.Error())
	}
	server = nil
	return nil
}

//export amnezia_xray_setsockcallback
func amnezia_xray_setsockcallback(cb C.amnezia_xray_sockcallback, ctx unsafe.Pointer) *C.char {
	callbacks.Lock()
	callbacks.socket, callbacks.socketContext = cb, ctx
	callbacks.Unlock()
	return nil
}

func LogHandlerCreator(cb C.amnezia_xray_loghandler, ctx unsafe.Pointer) cLog.WriterCreator {
	return func() cLog.Writer {
		return &logHandler{cb: cb, ctx: ctx}
	}
}

type logHandler struct {
	cb  C.amnezia_xray_loghandler
	ctx unsafe.Pointer
}

func (l *logHandler) Write(msg string) error {
	cMsg := C.CString(msg)
	defer C.free(unsafe.Pointer(cMsg))

	C.amnezia_xray_invokeloghandler(l.cb, cMsg, l.ctx)
	return nil
}

func (l *logHandler) Close() error {
	return nil
}

//export amnezia_xray_setloghandler
func amnezia_xray_setloghandler(cb C.amnezia_xray_loghandler, ctx unsafe.Pointer) {
	log.RegisterHandlerCreator(log.LogType_Console, func(lt log.LogType, options log.HandlerCreatorOptions) (cLog.Handler, error) {
		return cLog.NewLogger(LogHandlerCreator(cb, ctx)), nil
	})
}

func main() {
}
