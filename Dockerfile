FROM debian:12-slim AS windows_builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    wget \
    xz-utils \
    ninja-build \
    make \
    git \
    nasm \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt

RUN wget -q https://github.com/mstorsjo/llvm-mingw/releases/download/20251216/llvm-mingw-20251216-ucrt-ubuntu-22.04-x86_64.tar.xz \
    && tar -xf llvm-mingw-*.tar.xz \
    && mv llvm-mingw-*-x86_64 llvm-mingw \
    && rm llvm-mingw-*.tar.xz

RUN wget -q https://github.com/Kitware/CMake/releases/download/v4.2.1/cmake-4.2.1-linux-x86_64.tar.gz \
    && tar -zxvf cmake-4.2.1-linux-x86_64.tar.gz \
    && mv cmake-4.2.1-linux-x86_64 cmake \
    && rm cmake-4.2.1-linux-x86_64.tar.gz

ENV PATH="/opt/cmake/bin:/opt/llvm-mingw/bin:${PATH}"
ENV CC=x86_64-w64-mingw32-clang
ENV CXX=x86_64-w64-mingw32-clang++
ENV RC=x86_64-w64-mingw32-windres
ENV AR=llvm-ar
ENV RANLIB=llvm-ranlib

WORKDIR /opt/llvm-mingw/x86_64-w64-mingw32/include
RUN ln -s windows.h Windows.h && \
    ln -s tlhelp32.h TlHelp32.h && \
    ln -s dbghelp.h DbgHelp.h

RUN ln -s /usr/bin/make /usr/bin/mingw32-make

WORKDIR /project
COPY CMakeLists.txt .
COPY src/ ./src/

RUN cmake -B ./build -S . -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_SYSTEM_PROCESSOR=x86_64 \
    -DCMAKE_C_COMPILER=$CC \
    -DCMAKE_CXX_COMPILER=$CXX \
    -DCMAKE_RC_COMPILER=$RC \
    -DCMAKE_FIND_ROOT_PATH=/opt/llvm-mingw/x86_64-w64-mingw32 \
    -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY && \
    cmake --build ./build -j 2

FROM registry.access.redhat.com/ubi7/ubi-minimal AS linux_builder

RUN microdnf install -y bzip2 curl tar && microdnf clean all

RUN curl -Ls https://micro.mamba.pm/api/micromamba/linux-64/latest | tar -xvj -C /usr/local/bin --strip-components=1 bin/micromamba

ENV MAMBA_ROOT_PREFIX=/opt/micromamba
RUN micromamba create -n build_env -c conda-forge -y \
    gcc==15.2.0 gxx==15.2.0 \
    binutils ninja nasm cmake perl make git ccache && \
    micromamba clean -a -y

WORKDIR /project
COPY CMakeLists.txt .
COPY src/ ./src/

RUN micromamba run -n build_env cmake -B ./build -S . -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=gcc \
    -DCMAKE_CXX_COMPILER=g++ && \
    micromamba run -n build_env cmake --build ./build -j 2

FROM scratch

COPY --from=windows_builder /project/build/bin/MyProject.exe /windows/
COPY --from=linux_builder /project/build/bin/MyProject /linux/