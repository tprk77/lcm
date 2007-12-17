import commands
from distutils.core import setup, Extension


pkgconfig_cflags = commands.getoutput ("pkg-config --cflags glib-2.0 lcm")
pkgconfig_include_flags = commands.getoutput ("pkg-config --cflags-only-I lcm")
pkgconfig_include_dirs = [ t[2:] for t in pkgconfig_include_flags.split() ]

pkgconfig_lflags = commands.getoutput ( \
        "pkg-config --libs-only-l glib-2.0 lcm")
pkgconfig_libs = [ t[2:] for t in pkgconfig_lflags.split() ]

pkgconfig_biglflags = commands.getoutput ( \
        "pkg-config --libs-only-L glib-2.0 lcm")
pkgconfig_ldirs = [ t[2:] for t in pkgconfig_biglflags.split() ]

pylcm_extension = Extension("lcm._lcm",
                           sources=["module.c", "pyeventlog.c", "pylcm.c"],
                           library_dirs=pkgconfig_ldirs,
                           include_dirs=pkgconfig_include_dirs,
                           libraries=pkgconfig_libs,
                           extra_compile_args=['-Wno-strict-prototypes',
                               pkgconfig_cflags,
                               "-D_FILE_OFFSET_BITS=64",
                               "-D_LARGEFILE_SOURCE",
                               "-std=gnu99" ])

setup(name="lcm", version="0.1",
      ext_modules=[pylcm_extension],
      packages=["lcm"])
