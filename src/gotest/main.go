package main

import (
	"fmt"
	"net"

	"github.com/xtaci/kcp-go"
)

func rawUDP() {
	addr, _ := net.ResolveUDPAddr("udp", ":1234")
	udp, _ := net.ListenUDP("udp", addr)
	for {
		data := make([]byte, 1024)
		n, remote, err := udp.ReadFromUDP(data)
		if err != nil {
			fmt.Println(err, remote)
			continue
		}
		if n <= 0 {
			fmt.Println("zero data", remote)
		}
		if n > 0 {
			udp.WriteToUDP(data[:n], remote)
		}
	}
}

func handleEcho(conn *kcp.UDPSession) {
	conn.SetWindowSize(512, 512)
	conn.SetNoDelay(1, 10, 0, 0)
	buf := make([]byte, 65536)
	for {
		n, err := conn.Read(buf)
		if err != nil {
			panic(err)
		}
		fmt.Printf("read(%v) %v\n", n, string(buf[:n]))
		conn.Write(buf[:n])
	}
}

func main() {
	if listener, err := kcp.ListenWithOptions(":1234", nil, 0, 0); err == nil {
		for {
			if conn, err := listener.AcceptKCP(); err == nil {
				go handleEcho(conn)
			}
		}
	}
}
