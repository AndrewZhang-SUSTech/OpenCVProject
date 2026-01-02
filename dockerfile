FROM centos:7 AS builder

RUN sed -i 's/mirrorlist/#mirrorlist/g' /etc/yum.repos.d/CentOS-Base.repo && \
    sed -i 's|#baseurl=http://mirror.centos.org|baseurl=http://vault.centos.org|g' /etc/yum.repos.d/CentOS-Base.repo && \
    yum clean all && yum makecache && \
    yum install -y bzip2 curl && yum clean all

RUN curl -Ls https://micro.mamba.pm/api/micromamba/linux-64/latest | tar -xvj -C /usr/local/bin --strip-components=1 bin/micromamba

ENV MAMBA_ROOT_PREFIX=/opt/micromamba
RUN micromamba create -n build_env -c conda-forge -y \
    gcc==15.2.0 gxx==15.2.0 \
    ninja nasm cmake perl make git ccache && \
    micromamba clean -a -y

WORKDIR /project
COPY CMakeLists.txt .
COPY src/ ./src/

RUN micromamba run -n build_env cmake -B ./build -S . -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=gcc \
    -DCMAKE_CXX_COMPILER=g++ && \
    micromamba run -n build_env cmake --build ./build

FROM scratch

COPY --from=builder /project/build/bin/MyProject .
