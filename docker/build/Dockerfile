ARG ILLIXR_VERSION="x"
ARG PARENT_IMAGE="x"
ARG BUILD_FLAGS="x"

FROM ${PARENT_IMAGE}:${ILLIXR_VERSION}

USER illixr
RUN git clone -b master https://github.com/ILLIXR/ILLIXR.git --single-branch
RUN mkdir -p ILLIXR/build

RUN cmake -S/home/illixr/ILLIXR -B/home/illixr/ILLIXR/build -DCMAKE_INSTALL_PREFIX=/home/illixr -DBUILD_DOCS=ON -DYAML_FILE=profiles/all.yaml ${BUILD_FLAGS}
RUN cmake --build /home/illixr/ILLIXR/build -j4
RUN cmake --build /home/illixr/ILLIXR/build -t docs
RUN cmake --install /home/illixr/ILLIXR/build

RUN echo "\nexport LD_LIBRARY_PATH=/home/illixr/lib:\${LD_LIBRARY_PATH}\n" >> /home/illixr/.bashrc
RUN echo "export PATH=/home/illixr/bin:\${PATH}\n" >> /home/illixr/.bashrc
RUN echo "export PKG_CONFIG_PATH=/home/illixr/lib/pkgconfig\n" >> /home/illixr/.bashrc
RUN echo "export CMAKE_PREFIX_PATH=/home/illixr/lib/cmake\n" >> /home/illixr/.bashrc
