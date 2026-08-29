#!/bin/bash
# First-boot setup for the groovebox (runs as root via systemd.run, then reboots)
set +e

# hostname
echo "groovebox" > /etc/hostname
if grep -q "127.0.1.1" /etc/hosts; then
    sed -i "s/^127.0.1.1.*/127.0.1.1\tgroovebox/" /etc/hosts
else
    printf "127.0.1.1\tgroovebox\n" >> /etc/hosts
fi

# ssh on
systemctl enable ssh
systemctl start ssh

exit 0
