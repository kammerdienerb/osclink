#!/usr/bin/env bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

cd ${DIR}

mkdir -p build
rm -rf build/*

export DEBUG="yes"

server/build.sh || exit $?
client/build.sh || exit $?
