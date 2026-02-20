#!/bin/bash
# Ocean Blue - Esquema de cores para console TTY
for tty_dev in /dev/tty{1..6}; do
    if [ -e "$tty_dev" ]; then
        printf '\033]P00b1c2c' > "$tty_dev"
        printf '\033]P1c74b50' > "$tty_dev"
        printf '\033]P22e9e6e' > "$tty_dev"
        printf '\033]P3d4a856' > "$tty_dev"
        printf '\033]P42e6bb5' > "$tty_dev"
        printf '\033]P58f5a9e' > "$tty_dev"
        printf '\033]P63ca5a5' > "$tty_dev"
        printf '\033]P7a8c4d8' > "$tty_dev"
        printf '\033]P81e3a5f' > "$tty_dev"
        printf '\033]P9e87478' > "$tty_dev"
        printf '\033]PA5ec49e' > "$tty_dev"
        printf '\033]PBf0c674' > "$tty_dev"
        printf '\033]PC5c9fd7' > "$tty_dev"
        printf '\033]PDb88bc6' > "$tty_dev"
        printf '\033]PE6ecfcf' > "$tty_dev"
        printf '\033]PFdce8f0' > "$tty_dev"
    fi
done
