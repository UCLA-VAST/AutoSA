# Get the base Ubuntu image from Docker Hub
FROM ubuntu:latest
LABEL maintainer="jiewang@cs.ucla.edu"
ENV DEBIAN_FRONTEND=noninteractive 

# Update apps on the base image
RUN apt-get -y update && apt-get install -y

# Install the prerequisites
RUN apt-get -y install apt-utils automake autoconf libtool libtool-bin pkg-config libgmp3-dev libyaml-dev python3.6 python3-pip git wget cmake vim gdb  
RUN apt-get -y install libllvm-9-ocaml-dev libllvm9 llvm-9 llvm-9-dev llvm-9-doc llvm-9-examples llvm-9-runtime clang-9 clang-tools-9 clang-9-doc libclang-common-9-dev libclang-9-dev libclang1-9 clang-format-9 python-clang-9 clangd-9
RUN ln -s /usr/bin/llvm-config-9 /usr/bin/llvm-config

# Install NTL for barvinok
RUN mkdir /ntl
WORKDIR /ntl
RUN wget https://www.shoup.net/ntl/ntl-11.4.3.tar.gz
RUN gunzip ntl-11.4.3.tar.gz
RUN tar xf ntl-11.4.3.tar
WORKDIR /ntl/ntl-11.4.3/src
RUN ./configure NTL_GMP_LIP=on
RUN make -j4
RUN make install

# Copy the current folder to the Docker image
COPY . /usr/src/docker_autosa

# Specify the working directory
WORKDIR /usr/src/docker_autosa

# Install AutoSA
RUN pwd && ./install.sh
