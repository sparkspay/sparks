FROM phusion/baseimage:bionic-1.0.0
LABEL maintainer="Dash Developers <dev@sparks.org>"
LABEL description="Dockerised DashCore, built from Travis"

ARG USER_ID
ARG GROUP_ID

ENV HOME /home/sparks

# add user with specified (or default) user/group ids
ENV USER_ID ${USER_ID:-1000}
ENV GROUP_ID ${GROUP_ID:-1000}
RUN groupadd -g ${GROUP_ID} sparks && \
    useradd -u ${USER_ID} -g sparks -s /bin/bash -m -d /home/sparks sparks && \
    mkdir /home/sparks/.sparkscore && \
    chown ${USER_ID}:${GROUP_ID} -R /home/sparks

COPY bin/* /usr/local/bin/

RUN chmod a+x /usr/local/bin/*

USER sparks

VOLUME ["/home/sparks"]

EXPOSE 9998 9999 19998 19999

WORKDIR /home/sparks
