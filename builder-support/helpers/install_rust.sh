#!/bin/sh

set -e

echo "NYI: THIS SHOULD CHECK DIGEST OF DOWNLOADED FILE!"

cd /tmp
SITE=https://static.rust-lang.org/dist
SITE=https://www.drijf.net/pdns
RUST_VERSION=rust-1.70.0-x86_64-unknown-linux-gnu
RUST_TARBALL=$RUST_VERSION.tar.gz
curl -o $RUST_TARBALL $SITE/$RUST_TARBALL
tar -zxf $RUST_TARBALL 
cd $RUST_VERSION
./install.sh --prefix=/usr
cd ..
rm -rf $RUST_TARBALL $RUST_VERSION
