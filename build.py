#!/usr/bin/env python
#
# Copyright (C) 2015 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
from __future__ import print_function

import argparse
import datetime
import glob
import multiprocessing
import os
import shutil
import subprocess
import sys
import tempfile

import version


THIS_DIR = os.path.realpath(os.path.dirname(__file__))
ORIG_ENV = dict(os.environ)


def android_path(*args):
    return os.path.realpath(os.path.join(THIS_DIR, '../..', *args))


def build_path(subdir):
    # Our multistage build directories will be placed under OUT_DIR if it is in
    # the environment. By default they will be placed under
    # $ANDROID_BUILD_TOP/out.
    top_out = ORIG_ENV.get('OUT_DIR', android_path('out'))
    if not os.path.isabs(top_out):
        top_out = os.path.realpath(top_out)
    return os.path.join(top_out, subdir)


def short_version():
    return '.'.join([version.major, version.minor])


def long_version():
    return '.'.join([version.major, version.minor, version.patch])


def install_file(src, dst):
    print('Copying ' + src)
    shutil.copy2(src, dst)


def install_directory(src, dst):
    print('Copying ' + src)
    shutil.copytree(src, dst)


def build(out_dir):
    products = (
        'aosp_arm',
        'aosp_arm64',
        'aosp_mips',
        'aosp_mips64',
        'aosp_x86',
        'aosp_x86_64',
    )
    for product in products:
        build_product(out_dir, product)


def build_product(out_dir, product):
    env = dict(ORIG_ENV)
    env['OUT_DIR'] = out_dir
    env['DISABLE_LLVM_DEVICE_BUILDS'] = 'true'
    env['DISABLE_RELOCATION_PACKER'] = 'true'
    env['FORCE_BUILD_LLVM_COMPONENTS'] = 'true'
    env['FORCE_BUILD_SANITIZER_SHARED_OBJECTS'] = 'true'
    env['SKIP_LLVM_TESTS'] = 'true'
    env['TARGET_BUILD_VARIANT'] = 'userdebug'
    env['TARGET_PRODUCT'] = product

    jobs_arg = '-j{}'.format(multiprocessing.cpu_count())
    targets = ['clang-toolchain']
    subprocess.check_call(
        ['make', jobs_arg] + targets, cwd=android_path(), env=env)


def package_toolchain(build_dir, build_name, host):
    temp_dir = tempfile.mkdtemp()
    try:
        package_name = 'clang-' + build_name
        install_dir = os.path.join(temp_dir, package_name)
        install_toolchain(build_dir, install_dir, host)

        dist_dir = ORIG_ENV.get('DIST_DIR', build_dir)
        tarball_name = package_name + '-' + host
        package_path = os.path.join(dist_dir, tarball_name) + '.tar.bz2'
        print('Packaging ' + package_path)
        args = ['tar', '-cjC', temp_dir, '-f', package_path, package_name]
        subprocess.check_call(args)
    finally:
        shutil.rmtree(temp_dir)


def install_toolchain(build_dir, install_dir, host):
    install_built_host_files(build_dir, install_dir, host)
    install_analyzer_scripts(install_dir)
    install_headers(build_dir, install_dir)
    install_profile_rt(build_dir, install_dir, host)
    install_sanitizers(build_dir, install_dir, host)
    install_license_files(install_dir)
    install_repo_prop(install_dir)


def install_built_host_files(build_dir, install_dir, host):
    is_windows = host.startswith('windows')
    bin_ext = '.exe' if is_windows else ''
    lib_ext = '.dll' if is_windows else '.so'
    built_files = (
        'bin/clang' + bin_ext,
        'bin/clang++' + bin_ext,
        'bin/FileCheck' + bin_ext,
        'bin/llvm-as' + bin_ext,
        'bin/llvm-dis' + bin_ext,
        'bin/llvm-link' + bin_ext,
        'lib64/LLVMgold' + lib_ext,
        'lib64/libc++' + lib_ext,
        'lib64/libLLVM' + lib_ext,
    )
    for built_file in built_files:
        dirname = os.path.dirname(built_file)
        install_path = os.path.join(install_dir, dirname)
        if not os.path.exists(install_path):
            os.makedirs(install_path)

        built_path = os.path.join(build_dir, 'host', host, built_file)
        install_file(built_path, install_path)

        file_name = os.path.basename(built_file)
        subprocess.check_call(['strip', os.path.join(install_path, file_name)])


def install_analyzer_scripts(install_dir):
    tools_install_dir = os.path.join(install_dir, 'tools')
    os.makedirs(tools_install_dir)
    tools = ('scan-build', 'scan-view')
    tools_dir = android_path('external/clang/tools')
    for tool in tools:
        tool_path = os.path.join(tools_dir, tool)
        install_path = os.path.join(install_dir, 'tools', tool)
        install_directory(tool_path, install_path)


def install_headers(build_dir, install_dir):
    def should_copy(path):
        if os.path.basename(path) in ('Makefile', 'CMakeLists.txt'):
            return False
        _, ext = os.path.splitext(path)
        if ext == '.mk':
            return False
        return True

    headers_src = android_path('external/clang/lib/Headers')
    headers_dst = os.path.join(
        install_dir, 'lib/clang', short_version(), 'include')
    os.makedirs(headers_dst)
    for header in os.listdir(headers_src):
        if not should_copy(header):
            continue
        install_file(os.path.join(headers_src, header), headers_dst)

    install_file(android_path('bionic/libc/include/stdatomic.h'), headers_dst)

    arm_neon_h = os.path.join(
        build_dir, 'target/product/generic/obj/include/clang/arm_neon.h')
    install_file(arm_neon_h, headers_dst)

    os.symlink(short_version(),
               os.path.join(install_dir, 'lib/clang', long_version()))


def install_profile_rt(build_dir, install_dir, host):
    lib_dir = os.path.join(
        install_dir, 'lib/clang', short_version(), 'lib/linux')
    os.makedirs(lib_dir)

    install_target_profile_rt(build_dir, lib_dir)

    # We only support profiling libs for Linux and Android.
    if host == 'linux-x86':
        install_host_profile_rt(build_dir, host, lib_dir)


def install_target_profile_rt(build_dir, lib_dir):
    product_to_arch = {
        'generic': 'arm',
        'generic_arm64': 'arm64',
        'generic_mips': 'mips',
        'generic_mips64': 'mips64',
        'generic_x86': 'x86',
        'generic_x86_64': 'x86_64',
    }

    for product, arch in product_to_arch.items():
        product_dir = os.path.join(build_dir, 'target/product', product)
        static_libs = os.path.join(product_dir, 'obj/STATIC_LIBRARIES')
        built_lib = os.path.join(
            static_libs, 'libprofile_rt_intermediates/libprofile_rt.a')
        lib_name = 'libclang_rt.profile-{}-android.a'.format(arch)
        install_file(built_lib, os.path.join(lib_dir, lib_name))


def install_host_profile_rt(build_dir, host, lib_dir):
    arch_to_obj_dir = {
        'i686': 'obj32',
        'x86_64': 'obj',
    }

    for arch, obj_dir in arch_to_obj_dir.items():
        static_libs = os.path.join(
            build_dir, 'host', host, obj_dir, 'STATIC_LIBRARIES')
        built_lib = os.path.join(
            static_libs, 'libprofile_rt_intermediates/libprofile_rt.a')
        lib_name = 'libclang_rt.profile-{}.a'.format(arch)
        install_file(built_lib, os.path.join(lib_dir, lib_name))


def install_sanitizers(build_dir, install_dir, host):
    headers_src = android_path('external/compiler-rt/include/sanitizer')
    clang_lib = os.path.join(install_dir, 'lib/clang', short_version())
    headers_dst = os.path.join(clang_lib, 'include/sanitizer')
    install_directory(headers_src, headers_dst)

    # Tuples of (name, multilib).
    libs = (
        ('asan', True),
        ('asan_cxx', True),
        ('ubsan_standalone', True),
        ('ubsan_standalone_cxx', True),
        ('tsan', False),
        ('tsan_cxx', False),
    )

    obj32 = os.path.join(build_dir, 'host', host, 'obj32/STATIC_LIBRARIES')
    obj64 = os.path.join(build_dir, 'host', host, 'obj/STATIC_LIBRARIES')
    lib_dst = os.path.join(clang_lib, 'lib/linux')
    for lib, is_multilib in libs:
        built_lib_name = 'lib{}.a'.format(lib)

        obj64_dir = os.path.join(obj64, 'lib{}_intermediates'.format(lib))
        lib64_name = 'libclang_rt.{}-x86_64.a'
        built_lib64 = os.path.join(obj64_dir, built_lib_name)
        install_file(built_lib64, os.path.join(lib_dst, lib64_name))
        if is_multilib:
            obj32_dir = os.path.join(obj32, 'lib{}_intermediates'.format(lib))
            lib32_name = 'libclang_rt.{}-i686.a'
            built_lib32 = os.path.join(obj32_dir, built_lib_name)
            install_file(built_lib32, os.path.join(lib_dst, lib32_name))

    product_base_dir = os.path.join(build_dir, 'target/product')
    lib32_dir = os.path.join(product_base_dir, 'generic/system/lib')
    lib32_name = 'libclang_rt.asan-arm-android.so'
    install_file(os.path.join(lib32_dir, lib32_name), lib_dst)


def install_license_files(install_dir):
    projects = (
        'clang',
        'compiler-rt',
        'libcxx',
        'libcxxabi',
        'libunwind_llvm',
        'llvm',
    )

    notices = []
    for project in projects:
        project_path = android_path('external', project)
        license_pattern = os.path.join(project_path, 'MODULE_LICENSE_*')
        for license_file in glob.glob(license_pattern):
            install_file(license_file, install_dir)
        with open(os.path.join(project_path, 'NOTICE')) as notice_file:
            notices.append(notice_file.read())
    with open(os.path.join(install_dir, 'NOTICE'), 'w') as notice_file:
        notice_file.write('\n'.join(notices))


def install_repo_prop(install_dir):
    file_name = 'repo.prop'

    dist_dir = os.environ.get('DIST_DIR')
    if dist_dir is not None:
        dist_repo_prop = os.path.join(dist_dir, file_name)
        shutil.copy(dist_repo_prop, install_dir)
    else:
        out_file = os.path.join(install_dir, file_name)
        with open(out_file, 'w') as prop_file:
            cmd = [
                'repo', 'forall', '-c',
                'echo $REPO_PROJECT $(git rev-parse HEAD)',
            ]
            subprocess.check_call(cmd, stdout=prop_file)


def parse_args():
    parser = argparse.ArgumentParser()

    parser.add_argument(
        '--build-name', default=datetime.date.today().strftime('%Y%m%d'),
        help='Release name for the package.')

    multi_stage_group = parser.add_mutually_exclusive_group()
    multi_stage_group.add_argument(
        '--multi-stage', action='store_true',
        help='Perform multi-stage build (disabled by default).')
    multi_stage_group.add_argument(
        '--no-multi-stage', action='store_false', dest='multi_stage',
        help='Do not perform multi-stage build.')

    return parser.parse_args()


def main():
    args = parse_args()

    stage_1_out_dir = build_path('stage1')
    build(out_dir=stage_1_out_dir)
    final_out_dir = stage_1_out_dir
    if args.multi_stage:
        raise NotImplementedError

    # TODO(danalbert): Package Windows as part of the Linux build.
    # It looks like right now the build step isn't building the Windows clang.
    if sys.platform.startswith('linux'):
        host = 'linux-x86'
    elif sys.platform == 'darwin':
        host = 'darwin-x86'
    else:
        raise RuntimeError('Unsupported host: {}'.format(sys.platform))
    package_toolchain(final_out_dir, args.build_name, host)


if __name__ == '__main__':
    main()
