#!/bin/sh
set -euo pipefail

rm -f mp3.out
cc -o mp3.out -Wall mp3.c
echo
./mp3.out
