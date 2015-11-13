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
import argparse
import datetime
import multiprocessing
import os
import subprocess


THIS_DIR = os.path.realpath(os.path.dirname(__file__))
ORIG_ENV = dict(os.environ)


def android_path(path=''):
    return os.path.realpath(os.path.join(THIS_DIR, '../..', path))


def build_path(subdir):
    # Our multistage build directories will be placed under OUT_DIR if it is in
    # the environment. By default they will be placed under
    # $ANDROID_BUILD_TOP/out.
    top_out = ORIG_ENV.get('OUT_DIR', android_path('out'))
    if not os.path.isabs(top_out):
        top_out = os.path.realpath(top_out)
    return os.path.join(top_out, subdir)


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
    env['TARGET_PRODUCT'] = product

    jobs_arg = '-j{}'.format(multiprocessing.cpu_count())
    targets = ['clang-toolchain']
    subprocess.check_call(
        ['make', jobs_arg] + targets, cwd=android_path(), env=env)


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
    if args.multi_stage:
        raise NotImplementedError


if __name__ == '__main__':
    main()
