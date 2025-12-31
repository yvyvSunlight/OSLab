#!/bin/bash
set -e

SRC="/mnt/hgfs/ShareFiles/OSLab/"
TMP="$HOME/Desktop/OSLab_tmp/"
DST="/mnt/hgfs/ShareFiles/OSLab/"

echo "==> Syncing project to VM local disk..."
rsync -a --delete "$SRC" "$TMP"

cd "$TMP"

echo "==> Building image..."
make image

echo "==> Installing commands..."
cd command
make install
cd ..

echo "==> Syncing build results back to shared folder..."
rsync -a --delete "$TMP" "$DST"

echo "==> Cleaning up..."
rm -rf "$TMP"

echo "==> Done."
