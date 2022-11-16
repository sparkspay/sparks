FROM phusion/baseimage:bionic-1.0.0
LABEL maintainer="Sparks Developers <admin@sparkspay.io>"
LABEL description="Dockerised SparksCore, built from Travis"

ARG USER_ID
ARG GROUP_ID

ENV HOME /sparks

# add user with specified (or default) user/group ids
ENV USER_ID ${USER_ID:-1000}
ENV GROUP_ID ${GROUP_ID:-1000}
RUN groupadd -g ${GROUP_ID} sparks
RUN useradd -u ${USER_ID} -g sparks -s /bin/bash -m -d /sparks sparks
RUN mkdir /sparks/.sparkscore
RUN chown sparks:sparks -R /sparks

COPY bin/* /usr/local/bin/

RUN chmod a+x /usr/local/bin/*

USER sparks

VOLUME ["/sparks"]

EXPOSE 9998 9999 19998 19999

WORKDIR /sparks
