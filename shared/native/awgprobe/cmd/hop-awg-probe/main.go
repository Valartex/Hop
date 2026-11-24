package main

import (
	"hop/awg-probe"
	"os"
)

func main() {
	os.Exit(awgprobe.Run(os.Stdin, os.Stdout))
}
