from conan import ConanFile
from conan.tools.files import get, copy, apply_conandata_patches, export_conandata_patches, load, save
from conan.tools.layout import basic_layout
from conan.errors import ConanInvalidConfiguration
from conan.tools.env import Environment

import os
import stat
import shutil

from pathlib import Path

class AmneziaLibxray(ConanFile):
    name = "amnezia-libxray"
    version = "1.0.3"
    settings = "os", "arch", "compiler"

    def export_sources(self):
        export_conandata_patches(self)
        common = os.path.abspath(os.path.join(self.recipe_folder, "..", "..", "shared", "native"))
        copy(self, "*.go", src=os.path.join(self.recipe_folder, "..", "..", "platforms", "android", "native", "xray"), dst=os.path.join(self.export_sources_folder, "hop"))
        for name in ("probe.go", "session.go"):
            copy(self, name, src=os.path.join(common, "awgprobe"), dst=os.path.join(self.export_sources_folder, "hopprobe"))

    def configure(self):
        self.settings.rm_safe("compiler.libcxx")
        self.settings.rm_safe("compiler.cppstd")

    def layout(self):
        basic_layout(self, build_folder=".")

    def build_requirements(self):
        self.tool_requires("go/1.27.1")
    
    def validate(self):
        if self.settings.os != "Android":
            raise ConanInvalidConfiguration(f"{self.name} v{self.version} does not support {self.settings.os}")

    def source(self):
        get(self, f"https://github.com/amnezia-vpn/amnezia-libxray/archive/refs/tags/v{self.version}.zip",
            sha256="3b1194c2a76e73913fdae49983c40a219c45a164ebdae72ef1297469348de730", strip_root=True
        )

    def generate(self):
        env = Environment()
        ndk_path_str = self.conf.get("tools.android:ndk_path")
        if ndk_path_str:
            ndk_path = Path(ndk_path_str)
            if len(ndk_path.parts) > 2:
                sdk_path = ndk_path.parents[1]
                env.define("ANDROID_HOME", str(sdk_path))
        env.vars(self).save_script("conan_provide_androidhome")

    def _patch_sources(self):
        apply_conandata_patches(self)
        copy(self, "*.go", src=os.path.join(self.export_sources_folder, "hop"), dst=self.build_folder)
        copy(self, "*.go", src=os.path.join(self.export_sources_folder, "hopprobe"), dst=os.path.join(self.build_folder, "hopprobe"))
        module = os.path.join(self.build_folder, "go.mod")
        save(self, module, load(self, module).replace("amneziawg-go/v3 v3.0.1", "amneziawg-go/v3 v3.1.20260814"))
        build_path = os.path.join(self.build_folder, "build.sh")
        build_stat = os.stat(build_path)
        os.chmod(build_path, build_stat.st_mode | stat.S_IEXEC)

    def build(self):
        self._patch_sources()
        if self.settings_build.os == "Windows":
            # Нативная Windows-сборка не должна случайно запускать bash из WSL.
            self.run("go mod tidy")
            self.run("go run main/main.go")
            mobile_version = "v0.0.0-20260217195705-b56b3793a9c4"
            binary_dir = os.path.join(self.build_folder, "go-bin")
            environment = Environment()
            environment.define("GOBIN", binary_dir)
            environment.prepend_path("PATH", binary_dir)
            environment.define("ANDROID_NDK_HOME", str(self.conf.get("tools.android:ndk_path")))
            with environment.vars(self).apply():
                self.run(f"go install golang.org/x/mobile/cmd/gomobile@{mobile_version}")
                self.run(f"go install golang.org/x/mobile/cmd/gobind@{mobile_version}")
                destination = os.path.join(self.build_folder, "assets", "geo")
                os.makedirs(destination, exist_ok=True)
                for name in os.listdir(os.path.join(self.build_folder, "dat")):
                    shutil.copy2(os.path.join(self.build_folder, "dat", name), destination)
                arch = {"armv8": "arm64", "armv7": "arm", "x86_64": "amd64", "x86": "386"}[str(self.settings.arch)]
                self.run(f'gomobile bind -target android/{arch} -androidapi 28 '
                         '-javapkg=org.amnezia.vpn.protocol.xray -o libxray.aar '
                         '-ldflags="-w -s -buildid= -checklinkname=0 -extldflags=-Wl,-z,max-page-size=16384" -trimpath')
        else:
            self.run("./build.sh android")

    def package(self):
        copy(self, "libxray.aar", src=self.build_folder, dst=os.path.join(self.package_folder, "aar"))

    def package_info(self):
        self.cpp_info.set_property("cmake_extra_variables", {
            "AMNEZIA_LIBXRAY_PATH": Path(self.package_folder, "aar", "libxray.aar").as_posix(),
        })
