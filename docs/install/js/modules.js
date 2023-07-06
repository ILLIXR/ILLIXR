var ILLIXR_modules = '{\n' +
    '  "systems": [\n' +
    '    {\n' +
    '      "name": "CentOS",\n' +
    '      "versions": [\n' +
    '        "9"\n' +
    '      ]\n' +
    '    },\n' +
    '    {\n' +
    '      "name": "Fedora",\n' +
    '      "versions": [\n' +
    '        "37",\n' +
    '        "38"\n' +
    '      ]\n' +
    '    },\n' +
    '    {\n' +
    '      "name": "Ubuntu",\n' +
    '      "versions": [\n' +
    '        "20",\n' +
    '        "22"\n' +
    '      ]\n' +
    '    }\n' +
    '\n' +
    '  ],\n' +
    '  "plugins": [\n' +
    '    {\n' +
    '      "name": "Audio Pipeline",\n' +
    '      "cmake_flag": "USE_AUDIO_PIPELINE",\n' +
    '      "dependencies": [\n' +
    '        "git"\n' +
    '      ]\n' +
    '    },\n' +
    '    {\n' +
    '      "name": "debugview",\n' +
    '      "cmake_flag": "USE_DEBUGVIEW",\n' +
    '      "dependencies": [\n' +
    '        "glfw3"\n' +
    '      ]\n' +
    '    },\n' +
    '    {\n' +
    '      "name": "depthai",\n' +
    '      "cmake_flag": "USE_DEPTHAI",\n' +
    '      "dependencies": []\n' +
    '    },\n' +
    '    {\n' +
    '      "name": "gldemo",\n' +
    '      "cmake_flag": "USE_GLDEMO",\n' +
    '      "dependencies": []\n' +
    '    },\n' +
    '    {\n' +
    '      "name": "ground_truth_slam",\n' +
    '      "cmake_flag": "USE_GROUND_TRUTH_SLAM",\n' +
    '      "dependencies": []\n' +
    '    },\n' +
    '    {\n' +
    '      "name": "gtsam_integrator",\n' +
    '      "cmake_flag": "USE_GTSAM_INTEGRATOR",\n' +
    '      "dependencies": [\n' +
    '        "git"\n' +
    '      ]\n' +
    '    },\n' +
    '    {\n' +
    '      "name": "Kimera-VIO",\n' +
    '      "cmake_flag": "USE_KIMERA_VIO",\n' +
    '      "dependencies": [\n' +
    '        "git",\n' +
    '        "glog"\n' +
    '      ]\n' +
    '    },\n' +
    '    {\n' +
    '      "name": "Monado",\n' +
    '      "cmake_flag": "USE_MONADO",\n' +
    '      "dependencies": [\n' +
    '        "git",\n' +
    '        "glslang",\n' +
    '        "gflags",\n' +
    '        "JPEG",\n' +
    '        "PNG",\n' +
    '        "TIFF",\n' +
    '        "udev",\n' +
    '        "wayland-server",\n' +
    '        "x11-xcb",\n' +
    '        "xcb-glx",\n' +
    '        "xcb-randr",\n' +
    '        "xkbcommon",\n' +
    '        "OpenXR",\n' +
    '        "Vulkan",\n' +
    '        "libusb"\n' +
    '      ]\n' +
    '    },\n' +
    '    {\n' +
    '      "name": "offline_cam",\n' +
    '      "cmake_flag": "USE_OFFLINE_CAM",\n' +
    '      "dependencies": []\n' +
    '    },\n' +
    '    {\n' +
    '      "name": "offline_imu",\n' +
    '      "cmake_flag": "USE_OFFLINE_IMU",\n' +
    '      "dependencies": []\n' +
    '    },\n' +
    '    {\n' +
    '      "name": "offload_data",\n' +
    '      "cmake_flag": "USE_OFFLOAD_DATA",\n' +
    '      "dependencies": [\n' +
    '        "boost"\n' +
    '      ]\n' +
    '    },\n' +
    '    {\n' +
    '      "name": "offload_vio",\n' +
    '      "cmake_flag": "USE_OFFLOAD_VIO",\n' +
    '      "dependencies": [\n' +
    '        "git",\n' +
    '        "protobuf",\n' +
    '        "ecal"\n' +
    '      ]\n' +
    '    },\n' +
    '    {\n' +
    '      "name": "OpenXr_APP",\n' +
    '      "cmake_flag": "USE_OPENXR_APP",\n' +
    '      "dependencies": [\n' +
    '        "git",\n' +
    '        "SDL2"\n' +
    '      ]\n' +
    '    },\n' +
    '    {\n' +
    '      "name": "ORB_SLAM",\n' +
    '      "cmake_flag": "USE_ORB_SLAM",\n' +
    '      "dependencies": [\n' +
    '        "git",\n' +
    '        "boost",\n' +
    '        "fmt",\n' +
    '        "patch"\n' +
    '      ]\n' +
    '    },\n' +
    '    {\n' +
    '      "name": "passthrough_integrator",\n' +
    '      "cmake_flag": "USE_PASSTHROUGH_INTEGRATOR",\n' +
    '      "dependencies": []\n' +
    '    },\n' +
    '    {\n' +
    '      "name": "pose_lookup",\n' +
    '      "cmake_flag": "USE_POSE_LOOKUP",\n' +
    '      "dependencies": []\n' +
    '    },\n' +
    '    {\n' +
    '      "name": "pose_prediction",\n' +
    '      "cmake_flag": "USE_POSE_PREDICTION",\n' +
    '      "dependencies": []\n' +
    '    },\n' +
    '    {\n' +
    '      "name": "realsense",\n' +
    '      "cmake_flag": "USE_REALSENSE",\n' +
    '      "dependencies": [\n' +
    '        "realsense"\n' +
    '      ]\n' +
    '    },\n' +
    '    {\n' +
    '      "name": "record_imu_cam",\n' +
    '      "cmake_flag": "USE_RECORD_IMU_CAM",\n' +
    '      "dependencies": [\n' +
    '        "boost"\n' +
    '      ]\n' +
    '    },\n' +
    '    {\n' +
    '      "name": "rk4_integrator",\n' +
    '      "cmake_flag": "USE_RK4_INTEGRATOR",\n' +
    '      "dependencies": []\n' +
    '    },\n' +
    '    {\n' +
    '      "name": "timewarp_gl",\n' +
    '      "cmake_flag": "USE_TIMEWARP_GL",\n' +
    '      "dependencies": []\n' +
    '    }\n' +
    '  ],\n' +
    '  "dependencies": [\n' +
    '    {\n' +
    '      "boost": {\n' +
    '        "pkg": {\n' +
    '          "Ubuntu": {\n' +
    '            "20": {\n' +
    '              "pkg": "libboost-all-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "22": {\n' +
    '              "pkg": "libboost-all-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "Fedora": {\n' +
    '            "37": {\n' +
    '              "pkg": "boost-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "38": {\n' +
    '              "pkg": "boost-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "CentOS": {\n' +
    '            "9": {\n' +
    '              "pkg": "boost-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          }\n' +
    '        }\n' +
    '      }\n' +
    '    },\n' +
    '    {\n' +
    '      "ecal": {\n' +
    '        "pkg": {\n' +
    '          "Ubuntu": {\n' +
    '            "20": {\n' +
    '              "pkg": "graphviz zlib1g-dev qtbase5-dev libhdf5-dev libcurl4-openssl-dev libqwt-qt5-dev libyaml-cpp-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "22": {\n' +
    '              "pkg": "graphviz zlib1g-dev qtbase5-dev libhdf5-dev libcurl4-openssl-dev libqwt-qt5-dev libyaml-cpp-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "Fedora": {\n' +
    '            "37": {\n' +
    '              "pkg": "graphviz hdf5-devel libcurl-devel qwt-qt5-devel qt5-qtbase-devel libyaml-devel zlib-devel openssl-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "38": {\n' +
    '              "pkg": "graphviz hdf5-devel libcurl-devel qwt-qt5-devel qt5-qtbase-devel libyaml-devel zlib-devel openssl-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "CentOS": {\n' +
    '            "9": {\n' +
    '              "pkg": "graphviz hdf5-devel libcurl-devel qwt-qt5-devel qt5-qtbase-devel libyaml-devel zlib-devel openssl-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          }\n' +
    '        }\n' +
    '      }\n' +
    '    },\n' +
    '    {\n' +
    '      "fmt": {\n' +
    '        "pkg": {\n' +
    '          "Ubuntu": {\n' +
    '            "20": {\n' +
    '              "pkg": "libfmt-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "22": {\n' +
    '              "pkg": "libfmt-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "Fedora": {\n' +
    '            "37": {\n' +
    '              "pkg": "fmt-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "38": {\n' +
    '              "pkg": "fmt-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "CentOS": {\n' +
    '            "9": {\n' +
    '              "pkg": "fmt-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          }\n' +
    '        }\n' +
    '      }\n' +
    '    },\n' +
    '    {\n' +
    '      "gflags": {\n' +
    '        "pkg": {\n' +
    '          "Ubuntu": {\n' +
    '            "20": {\n' +
    '              "pkg": "libgflags-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "22": {\n' +
    '              "pkg": "libgflags-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "Fedora": {\n' +
    '            "37": {\n' +
    '              "pkg": "gflags-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "38": {\n' +
    '              "pkg": "gflags-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "CentOS": {\n' +
    '            "9": {\n' +
    '              "pkg": "gflags-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          }\n' +
    '        }\n' +
    '      }\n' +
    '    },\n' +
    '    {\n' +
    '      "git": {\n' +
    '        "pkg": {\n' +
    '          "Ubuntu": {\n' +
    '            "20": {\n' +
    '              "pkg": "git",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "22": {\n' +
    '              "pkg": "git",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "Fedora": {\n' +
    '            "37": {\n' +
    '              "pkg": "git",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "38": {\n' +
    '              "pkg": "git",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "CentOS": {\n' +
    '            "9": {\n' +
    '              "pkg": "git",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          }\n' +
    '        }\n' +
    '      }\n' +
    '    },\n' +
    '    {\n' +
    '      "glfw3": {\n' +
    '        "pkg": {\n' +
    '          "Ubuntu": {\n' +
    '            "20": {\n' +
    '              "pkg": "libglfw3-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "22": {\n' +
    '              "pkg": "libglfw3-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "Fedora": {\n' +
    '            "37": {\n' +
    '              "pkg": "glfw-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "38": {\n' +
    '              "pkg": "glfw-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "CentOS": {\n' +
    '            "9": {\n' +
    '              "pkg": "glfw-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          }\n' +
    '        }\n' +
    '      }\n' +
    '    },\n' +
    '    {\n' +
    '      "glog": {\n' +
    '        "pkg": {\n' +
    '          "Ubuntu": {\n' +
    '            "20": {\n' +
    '              "pkg": "libgoogle-glog-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "22": {\n' +
    '              "pkg": "libgoogle-glog-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "Fedora": {\n' +
    '            "37": {\n' +
    '              "pkg": "glog-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "38": {\n' +
    '              "pkg": "glog-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "CentOS": {\n' +
    '            "9": {\n' +
    '              "pkg": "glog-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          }\n' +
    '        }\n' +
    '      }\n' +
    '    },\n' +
    '    {\n' +
    '      "glslang": {\n' +
    '        "pkg": {\n' +
    '          "Ubuntu": {\n' +
    '            "20": {\n' +
    '              "pkg": "glslang-dev glslang-tools",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "22": {\n' +
    '              "pkg": "glslang-dev glslang-tools",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "Fedora": {\n' +
    '            "37": {\n' +
    '              "pkg": "glslang-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "38": {\n' +
    '              "pkg": "glslang-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "CentOS": {\n' +
    '            "9": {\n' +
    '              "pkg": "glslang-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          }\n' +
    '        }\n' +
    '      }\n' +
    '    },\n' +
    '    {\n' +
    '      "JPEG": {\n' +
    '        "pkg": {\n' +
    '          "Ubuntu": {\n' +
    '            "20": {\n' +
    '              "pkg": "libjpeg-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "22": {\n' +
    '              "pkg": "libjpeg-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "Fedora": {\n' +
    '            "37": {\n' +
    '              "pkg": "libjpeg-turbo-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "38": {\n' +
    '              "pkg": "libjpeg-turbo-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "CentOS": {\n' +
    '            "9": {\n' +
    '              "pkg": "libjpeg-turbo-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          }\n' +
    '        }\n' +
    '      }\n' +
    '    },\n' +
    '    {\n' +
    '      "libusb": {\n' +
    '        "pkg": {\n' +
    '          "Ubuntu": {\n' +
    '            "20": {\n' +
    '              "pkg": "libusb-1.0.0-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "22": {\n' +
    '              "pkg": "libusb-1.0.0-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "Fedora": {\n' +
    '            "37": {\n' +
    '              "pkg": "libusb1-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "38": {\n' +
    '              "pkg": "libusb1-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "CentOS": {\n' +
    '            "9": {\n' +
    '              "pkg": "libusb1-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          }\n' +
    '        }\n' +
    '      }\n' +
    '    },\n' +
    '    {\n' +
    '      "OpenXR": {\n' +
    '        "pkg": {\n' +
    '          "Ubuntu": {\n' +
    '            "20": {\n' +
    '              "pkg": "libopenxr-dev libopenxr1-monado",\n' +
    '              "postnotes": "",\n' +
    '              "notes": "Ubuntu 20 does not have a system supplied package for OpenXR, bit it can be installed from the vendor by adding the following repo to your system:<div class=\\"code-box-copy\\">\\n<button class=\\"code-box-copy__btn\\" data-clipboard-target=\\"#openU20\\" title=\\"Copy\\"></button><pre class=\\"language-shell\\"><code class=\\"language-shell\\" id=\\"openU20\\">sudo apt-get -y install wget software-properties-common\\nsudo apt-get update\\nsudo add-apt-repository ppa:monado-xr/monado -y -u\\nsudo wget -nv https://monado.freedesktop.org/keys/monado-ci.asc -O /etc/apt/trusted.gpg.d/monado-ci.asc\\nsudo echo \'deb https://monado.pages.freedesktop.org/monado/apt focal main\' | tee /etc/apt/sources.list.d/monado-ci.list\\nsudo apt update</pre></code></div>"\n' +
    '            },\n' +
    '            "22": {\n' +
    '              "pkg": "libopenxr-dev libopenxr1-monado",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "Fedora": {\n' +
    '            "37": {\n' +
    '              "pkg": "openxr-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "38": {\n' +
    '              "pkg": "openxr-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "CentOS": {\n' +
    '            "9": {\n' +
    '              "pkg": "openxr-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          }\n' +
    '        }\n' +
    '      }\n' +
    '    },\n' +
    '    {\n' +
    '      "patch": {\n' +
    '        "pkg": {\n' +
    '          "Ubuntu": {\n' +
    '            "20": {\n' +
    '              "pkg": "patch",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "22": {\n' +
    '              "pkg": "patch",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "Fedora": {\n' +
    '            "37": {\n' +
    '              "pkg": "patch",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "38": {\n' +
    '              "pkg": "patch",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "CentOS": {\n' +
    '            "9": {\n' +
    '              "pkg": "patch",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          }\n' +
    '        }\n' +
    '      }\n' +
    '    },\n' +
    '    {\n' +
    '      "PNG": {\n' +
    '        "pkg": {\n' +
    '          "Ubuntu": {\n' +
    '            "20": {\n' +
    '              "pkg": "libpng-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "22": {\n' +
    '              "pkg": "libpng-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "Fedora": {\n' +
    '            "37": {\n' +
    '              "pkg": "libpng-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "38": {\n' +
    '              "pkg": "libpng-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "CentOS": {\n' +
    '            "9": {\n' +
    '              "pkg": "libpng-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          }\n' +
    '        }\n' +
    '      }\n' +
    '    },\n' +
    '    {\n' +
    '      "protobuf": {\n' +
    '        "pkg": {\n' +
    '          "Ubuntu": {\n' +
    '            "20": {\n' +
    '              "pkg": "libprotobuf-dev libprotoc-dev protobuf-compiler",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "22": {\n' +
    '              "pkg": "libprotobuf-dev libprotoc-dev protobuf-compiler",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "Fedora": {\n' +
    '            "37": {\n' +
    '              "pkg": "protobuf-devel protobuf-compiler",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "38": {\n' +
    '              "pkg": "protobuf-devel protobuf-compiler",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "CentOS": {\n' +
    '            "9": {\n' +
    '              "pkg": "protobuf-devel protobuf-compiler",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          }\n' +
    '        }\n' +
    '      }\n' +
    '    },\n' +
    '    {\n' +
    '      "realsense": {\n' +
    '        "pkg": {\n' +
    '          "Ubuntu": {\n' +
    '            "20": {\n' +
    '              "pkg": "librealsense2-dev librealsense2-gl-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": "Ubuntu does not have a system supplied package for librealsense, but it can be installed from the vendor (Intel) by adding the following repo to your system:<div class=\\"code-box-copy\\">\\n<button class=\\"code-box-copy__btn\\" data-clipboard-target=\\"#realU20\\" title=\\"Copy\\"></button><pre class=\\"language-shell\\"><code class=\\"language-shell\\" id=\\"realU20\\">sudo apt-get -y install curl apt-transport-https lsb-core\\nsudo mkdir -p /etc/apt/keyrings\\nsudo curl -sSf https://librealsense.intel.com/Debian/librealsense.pgp | tee /etc/apt/keyrings/librealsense.pgp > /dev/null\\nsudo echo \\"deb [signed-by=/etc/apt/keyrings/librealsense.pgp] https://librealsense.intel.com/Debian/apt-repo `lsb_release -cs` main\\" | tee /etc/apt/sources.list.d/librealsense.list > /dev/null\\nsudo apt-get update</pre></code></div>"\n' +
    '            },\n' +
    '            "22": {\n' +
    '              "pkg": "librealsense2-dev librealsense2-gl-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": "Ubuntu does not have a system supplied package for librealsense, but it can be installed from the vendor (Intel) by adding the following repo to your system:<div class=\\"code-box-copy\\">\\n<button class=\\"code-box-copy__btn\\" data-clipboard-target=\\"#realU22\\" title=\\"Copy\\"></button><pre class=\\"language-shell\\"><code class=\\"language-shell\\" id=\\"realU22\\">sudo apt-get -y install curl apt-transport-https lsb-core\\nsudo mkdir -p /etc/apt/keyrings\\nsudo curl -sSf https://librealsense.intel.com/Debian/librealsense.pgp | tee /etc/apt/keyrings/librealsense.pgp > /dev/null\\nsudo echo \\"deb [signed-by=/etc/apt/keyrings/librealsense.pgp] https://librealsense.intel.com/Debian/apt-repo `lsb_release -cs` main\\" | tee /etc/apt/sources.list.d/librealsense.list > /dev/null\\nsudo apt-get update</pre></code></div>"\n' +
    '            }\n' +
    '          },\n' +
    '          "Fedora": {\n' +
    '            "37": {\n' +
    '              "pkg": "librealsense-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "38": {\n' +
    '              "pkg": "librealsense-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "CentOS": {\n' +
    '            "9": {\n' +
    '              "pkg": "git zip unzip libtool",\n' +
    '              "postnotes": "Centos does not provide a vendor version of librealsense, but it can be automatically compiled and installed with the following, possibly as sudo (run after installing the above packages):<div class=\\"code-box-copy\\">\\n<button class=\\"code-box-copy__btn\\" data-clipboard-target=\\"#realC9\\" title=\\"Copy\\"></button><pre class=\\"language-shell\\"><code class=\\"language-shell\\" id=\\"realC9\\">git clone https://github.com/Microsoft/vcpkg.git\\ncd vcpkg\\n./bootstrap-vcpkg.sh\\n./vcpkg integrate install\\n./vcpkg install realsense2</pre></code></div>",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          }\n' +
    '        }\n' +
    '      }\n' +
    '    },\n' +
    '    {\n' +
    '      "SDL2": {\n' +
    '        "pkg": {\n' +
    '          "Ubuntu": {\n' +
    '            "20": {\n' +
    '              "pkg": "libsdl2-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "22": {\n' +
    '              "pkg": "libsdl2-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "Fedora": {\n' +
    '            "37": {\n' +
    '              "pkg": "SDL2-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "38": {\n' +
    '              "pkg": "SDL2-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "CentOS": {\n' +
    '            "9": {\n' +
    '              "pkg": "SDL2-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          }\n' +
    '        }\n' +
    '      }\n' +
    '    },\n' +
    '    {\n' +
    '      "TIFF": {\n' +
    '        "pkg": {\n' +
    '          "Ubuntu": {\n' +
    '            "20": {\n' +
    '              "pkg": "libtiff-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "22": {\n' +
    '              "pkg": "libtiff-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "Fedora": {\n' +
    '            "37": {\n' +
    '              "pkg": "libtiff-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "38": {\n' +
    '              "pkg": "libtiff-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "CentOS": {\n' +
    '            "9": {\n' +
    '              "pkg": "libtiff-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          }\n' +
    '        }\n' +
    '      }\n' +
    '    },\n' +
    '    {\n' +
    '      "udev": {\n' +
    '        "pkg": {\n' +
    '          "Ubuntu": {\n' +
    '            "20": {\n' +
    '              "pkg": "udev libudev-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "22": {\n' +
    '              "pkg": "udev libudev-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "Fedora": {\n' +
    '            "37": {\n' +
    '              "pkg": "systemd-udev systemd-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "38": {\n' +
    '              "pkg": "systemd-udev systemd-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "CentOS": {\n' +
    '            "9": {\n' +
    '              "pkg": "systemd systemd-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          }\n' +
    '        }\n' +
    '      }\n' +
    '    },\n' +
    '    {\n' +
    '      "Vulkan": {\n' +
    '        "pkg": {\n' +
    '          "Ubuntu": {\n' +
    '            "20": {\n' +
    '              "pkg": "libvulkan-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "22": {\n' +
    '              "pkg": "libvulkan-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "Fedora": {\n' +
    '            "37": {\n' +
    '              "pkg": "vulkan-loader-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "38": {\n' +
    '              "pkg": "vulkan-loader-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "CentOS": {\n' +
    '            "9": {\n' +
    '              "pkg": "vulkan-loader-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          }\n' +
    '        }\n' +
    '      }\n' +
    '    },\n' +
    '    {\n' +
    '      "wayland-server": {\n' +
    '        "pkg": {\n' +
    '          "Ubuntu": {\n' +
    '            "20": {\n' +
    '              "pkg": "libwayland-dev wayland-protocols",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "22": {\n' +
    '              "pkg": "libwayland-dev wayland-protocols",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "Fedora": {\n' +
    '            "37": {\n' +
    '              "pkg": "wayland-devel wayland-protocols-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "38": {\n' +
    '              "pkg": "wayland-devel wayland-protocols-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "CentOS": {\n' +
    '            "9": {\n' +
    '              "pkg": "wayland-devel wayland-protocols-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          }\n' +
    '        }\n' +
    '      }\n' +
    '    },\n' +
    '    {\n' +
    '      "x11-xcb": {\n' +
    '        "pkg": {\n' +
    '          "Ubuntu": {\n' +
    '            "20": {\n' +
    '              "pkg": "libx11-xcb-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "22": {\n' +
    '              "pkg": "libx11-xcb-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "Fedora": {\n' +
    '            "37": {\n' +
    '              "pkg": "libX11-xcb libxcb-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "38": {\n' +
    '              "pkg": "libX11-xcb libxcb-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "CentOS": {\n' +
    '            "9": {\n' +
    '              "pkg": "libX11-xcb libxcb-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          }\n' +
    '        }\n' +
    '      }\n' +
    '    },\n' +
    '    {\n' +
    '      "xcb-glx": {\n' +
    '        "pkg": {\n' +
    '          "Ubuntu": {\n' +
    '            "20": {\n' +
    '              "pkg": "libxcb-glx0-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "22": {\n' +
    '              "pkg": "libxcb-glx0-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "Fedora": {\n' +
    '            "37": {\n' +
    '              "pkg": "",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "38": {\n' +
    '              "pkg": "",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "CentOS": {\n' +
    '            "9": {\n' +
    '              "pkg": "",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          }\n' +
    '        }\n' +
    '      }\n' +
    '    },\n' +
    '    {\n' +
    '      "xcb-randr": {\n' +
    '        "pkg": {\n' +
    '          "Ubuntu": {\n' +
    '            "20": {\n' +
    '              "pkg": "libxcb-randr0-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "22": {\n' +
    '              "pkg": "libxcb-randr0-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "Fedora": {\n' +
    '            "37": {\n' +
    '              "pkg": "",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "38": {\n' +
    '              "pkg": "",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "CentOS": {\n' +
    '            "9": {\n' +
    '              "pkg": "",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          }\n' +
    '        }\n' +
    '      }\n' +
    '    },\n' +
    '    {\n' +
    '      "xkbcommon": {\n' +
    '        "pkg": {\n' +
    '          "Ubuntu": {\n' +
    '            "20": {\n' +
    '              "pkg": "libxkbcommon-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "22": {\n' +
    '              "pkg": "libxkbcommon-dev",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "Fedora": {\n' +
    '            "37": {\n' +
    '              "pkg": "libxkbcommon-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            },\n' +
    '            "38": {\n' +
    '              "pkg": "libxkbcommon-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          },\n' +
    '          "CentOS": {\n' +
    '            "9": {\n' +
    '              "pkg": "libxkbcommon-devel",\n' +
    '              "postnotes": "",\n' +
    '              "notes": ""\n' +
    '            }\n' +
    '          }\n' +
    '        }\n' +
    '      }\n' +
    '    }\n' +
    '  ],\n' +
    '  "groups": [\n' +
    '    {\n' +
    '      "name": "CI",\n' +
    '      "plugins": ["Audio Pipeline", "gldemo", "ground_truth_slam", "gtsam_integrator", "Kimera-VIO", "offline_imu", "offline_cam", "pose_prediction", "timewarp_gl"]\n' +
    '    },\n' +
    '    {\n' +
    '      "name": "Monado",\n' +
    '      "plugins": ["Audio Pipeline", "gtsam_integrator", "Kimera-VIO", "Monado", "offline_imu", "offline_cam", "OpenXr_APP", "pose_prediction", "timewarp_gl"]\n' +
    '    },\n' +
    '    {\n' +
    '      "name": "Native",\n' +
    '      "plugins": ["Audio Pipeline", "debugview", "gtsam_integrator", "ground_truth_slam", "gldemo", "Kimera-VIO", "offline_imu", "offline_cam", "offload_data", "pose_prediction", "timewarp_gl"]\n' +
    '    }\n' +
    '  ]\n' +
    '}';