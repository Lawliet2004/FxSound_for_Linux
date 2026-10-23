FROM ubuntu@sha256:c098726dd9b9733f3e885f8de0ef375206a311b0a317eadc40d347b1115fb944
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update -qq && apt-get install -y --no-install-recommends python3 xvfb xauth x11-utils dbus-x11 libglib2.0-bin procps systemd pipewire-bin pipewire-pulse pulseaudio-utils wireplumber && rm -rf /var/lib/apt/lists/*
