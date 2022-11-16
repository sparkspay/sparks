#!/usr/bin/env bash

export LC_ALL=C

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd $DIR/.. || exit

DOCKER_IMAGE=${DOCKER_IMAGE:-sparkspay/sparksd-develop}
DOCKER_TAG=${DOCKER_TAG:-latest}

BUILD_DIR=${BUILD_DIR:-.}

rm docker/bin/*
mkdir docker/bin
cp $BUILD_DIR/src/sparksd docker/bin/
cp $BUILD_DIR/src/sparks-cli docker/bin/
cp $BUILD_DIR/src/sparks-tx docker/bin/
strip docker/bin/sparksd
strip docker/bin/sparks-cli
strip docker/bin/sparks-tx

docker build --pull -t $DOCKER_IMAGE:$DOCKER_TAG -f docker/Dockerfile docker
