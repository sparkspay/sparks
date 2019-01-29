FROM debian:stretch
LABEL maintainer="Dash Developers <dev@sparkspay.io>"
LABEL description="Dockerised DashCore, built from Travis"

RUN apt-get update && apt-get -y upgrade && apt-get clean && rm -fr /var/cache/apt/*

COPY bin/* /usr/bin/
