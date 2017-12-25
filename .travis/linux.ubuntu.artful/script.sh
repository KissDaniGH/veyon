#!/usr/bin/env bash

set -e

/veyon/.travis/common/linux-build.sh /veyon /build

cd /build

rename "s/_amd64/-ubuntu-artful_amd64/g" *.deb

dpkg -I *.deb

mv -v *.deb /veyon

