FROM debian:stretch
LABEL maintainer="Sparks Developers <admin@sparkspay.io>"
LABEL description="Dockerised SparksCore, built from Travis"

RUN apt-get update && apt-get -y upgrade && apt-get clean && rm -fr /var/cache/apt/*

COPY bin/* /usr/bin/
