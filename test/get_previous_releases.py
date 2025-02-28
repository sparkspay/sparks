#!/usr/bin/env python3
#
# Copyright (c) 2018-2020 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Download or build previous releases.
# Needs curl and tar to download a release, or the build dependencies when
# building a release.

import argparse
import contextlib
from fnmatch import fnmatch
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import hashlib


SHA256_SUMS = {
    "c78326cbf62d7f43c108d9bea1f703114d22327506e96e9994a1b647eb57701a":  "sparkscore-0.19.3.0-aarch64-linux-gnu.tar.gz",
    "125905eeca864db1ef2e86fd0758adfeb207d624520306b5de3947d40425be8b":  "sparkscore-0.19.3.0-osx-unsigned.dmg",
    "9a56539c8c93af4416336d9497af56538462c13abc25b49c7871bd4efccb76c1":  "sparkscore-0.19.3.0-osx64.tar.gz",
    "f8b44c47a69f5d55d299506645f10d6b7578a63292405ceea86b0c3373a14962":  "sparkscore-0.19.3.0-riscv64-linux-gnu.tar.gz",
    "fd5f2e17af957d7c62bbb4803a270503870a507201d995e0baa488335005d515":  "sparkscore-0.19.3.0-win64-setup-unsigned.exe",
    "3751abb3c65612d910488ea9d46aba0a4f21569688e9d3287289f4572d290c48":  "sparkscore-0.19.3.0-win64.zip",
    "901829a74729cc9ccbce4acfbef795aee0d519fa3fede1c510cbf1a60774351a":  "sparkscore-0.19.3.0-x86_64-linux-gnu.tar.gz",
    #
    "8042e799f591996c563ffb0427dab83846e7f7a64c71da8513961344261a2999":  "sparkscore-0.18.2.3-aarch64-linux-gnu.tar.gz",
    "22caa95481eb487f4556f223b5540a2974fee3385cd0e571c622dcf10166c823":  "sparkscore-0.18.2.3-osx-unsigned.dmg",
    "e0b2ac766d1671a2cbd5cf231c142d023f4ad0539b2d4bdbec81bdfa2b26db3a":  "sparkscore-0.18.2.3-osx64.tar.gz",
    "eeef9c5f0a74841e46dea4bea3a2a5e3667abb898c0c47270bcb0b80236fe991":  "sparkscore-0.18.2.3-riscv64-linux-gnu.tar.gz",
    "0a9a9e4e8d61c003cd011a4c3d3b7752ad92ddaca7cfd5e25e8c76eaea54eecb":  "sparkscore-0.18.2.3-win64-setup-unsigned.exe",
    "2713bf47e0750cca81748733caf3b595602ade6a02f7f84c20a673aae9237628":  "sparkscore-0.18.2.3-win64.zip",
    "8e83305c6509384a56b13010e15949ed010d612737b8a672986d1025afc7f5e1":  "sparkscore-0.18.2.3-x86_64-linux-gnu.tar.gz",
    #
    "ba07aec9efa0d83433feea7c486450d3b0a1bba4a6061378d9632172cb7614e4":  "sparkscore-0.17.3-aarch64-linux-gnu.tar.gz",
    "886814f4cc229eb3a0f298a99c63f5520ddfb044a5a7249041355a4f54e9d5a5":  "sparkscore-0.17.3-arm-linux-gnueabihf.tar.gz",
    "cccec5a1c2d6893c092d7537fa14d7e6da43c7f0b33929ccda4374ee4687d907":  "sparkscore-0.17.3-i686-pc-linux-gnu.tar.gz",
    "8b50c2e35d6e847b8f76eb2522bca15fbd2fc03db6980f6120cbf1805fbb556c":  "sparkscore-0.17.3-osx64.tar.gz",
    "979585fd3edeb08d0571b42b6ac23f0670920f67f137cd19968aa41320c75b00":  "sparkscore-0.17.3-osx-unsigned.dmg",
    "69c654a0c35aabc7ebb250f34d16c7418bd51ed2406a2ce2344a2f396a80ed70":  "sparkscore-0.17.3-win32-setup-unsigned.exe",
    "51ae15a6903ff86c8eb76436d88e92f3a460fb04d7307ba599fbfcfa98b30c0d":  "sparkscore-0.17.3-win32.zip",
    "6e0f7ee2ade6d338a5d32be0e9ccaa8b1eb73a775cfb47f018118c502bab35ff":  "sparkscore-0.17.3-win64-setup-unsigned.exe",
    "57ebc6517d2c6bbd75e129b933323572928aacb82c0df79acafa6bf249250c6a":  "sparkscore-0.17.3-win64.zip",
    "89a3c03a1d38cf7970562a5b3814182b9fe7882aa0c9a6b72043309339a127c1":  "sparkscore-0.17.3-x86_64-linux-gnu.tar.gz",
    #
    "0a4294f0f40d02362842c18de6e3ea298a02ab551093c139dd4bb1f6b17dd5be":  "sparkscore-0.16.0-aarch64-linux-gnu.tar.gz",
    "921fcdff4072857fc1afde5abec43d810f2bb4bad960d1ed8b83cfcc9c6eb771":  "sparkscore-0.16.0-arm-linux-gnueabihf.tar.gz",
    "1e15b88dc2985c62568a920f3808337a6351d8999f98a93d4ba0cbfe9b52ce54":  "sparkscore-0.16.0-i686-pc-linux-gnu.tar.gz",
    "1f805e862d34bdd42a85fdc0d3f67bc10ba547590417956cd57c2c8540a56be3":  "sparkscore-0.16.0-osx64.tar.gz",
    "73e24f15d1fb31c849c74ccf06c01635d677e4212cae926427b494d43480559a":  "sparkscore-0.16.0-osx-unsigned.dmg",
    "2ad311f6e82d759102fdf5bd307bcc907b8aa5bf1d750bd0432e45d4d1677c6f":  "sparkscore-0.16.0-win32-setup-unsigned.exe",
    "3ac68e8e72b5c0e0a6b0dcaf22f0e615f5655c0f14b1433371f017a0d6ab21a9":  "sparkscore-0.16.0-win32.zip",
    "470b78f82aba77d4952a831689f2ece869ec5d53c8564cc26677325b23df0921":  "sparkscore-0.16.0-win64-setup-unsigned.exe",
    "d263d082a569dcbdc9ab99da0a2763e6df0e6d70aa36e82c1b93cc83da657fb8":  "sparkscore-0.16.0-win64.zip",
    "14a755ecc65b26993580a28737af2d8800aaf7782bd63094d7e48cf00a2809e6":  "sparkscore-0.16.0-x86_64-linux-gnu.tar.gz",
    #
    "f33acc30b033b6998319cb8247d4e6c6d8db92c813358ee70ed3b96d1cdb7afe":  "sparkscore-0.15.0-aarch64-linux-gnu.tar.gz",
    "aa829b979a5f4f9d81cc11aabccae7913857737fa7dc68e69b1f489cf569091c":  "sparkscore-0.15.0-arm-linux-gnueabihf.tar.gz",
    "85fbd97242ada1e5698d8e68c6727f450d3d83dfd488e5fc490f71f2d032ae71":  "sparkscore-0.15.0-i686-pc-linux-gnu.tar.gz",
    "bbb3a876177a6e2aa4ef9fdd83de4f70ce9d40a912fbc91d09686cc9c2efd131":  "sparkscore-0.15.0-osx64.tar.gz",
    "845085d36cde159f56f16ccd2efb41a8c9a5c6400c022b6b87956f8c3e39c281":  "sparkscore-0.15.0-osx-unsigned.dmg",
    "615e1980ce68fe7c5db2b23a41646deb2c582de7567c51101eef65488a9a3fbe":  "sparkscore-0.15.0-win32-setup-unsigned.exe",
    "416222cb1e5a179f96390daed278f15c5030c21a9cdf187417c51ef38656fc3e":  "sparkscore-0.15.0-win32.zip",
    "a05b793288bc1f9ceb7d1f07972ed0fdb102c2b4ed1c1e06bab0b07f8780b421":  "sparkscore-0.15.0-win64-setup-unsigned.exe",
    "8e13d03728979ed8a5e495313c9a2f51ef13dcb8d763f923a71d584e0deb1cfc":  "sparkscore-0.15.0-win64.zip",
    "b78e387a53763c5298a6ca99f8a9e03b17039478818ded69cca5234395a12a25":  "sparkscore-0.15.0-x86_64-linux-gnu.tar.gz",
}


@contextlib.contextmanager
def pushd(new_dir) -> None:
    previous_dir = os.getcwd()
    os.chdir(new_dir)
    try:
        yield
    finally:
        os.chdir(previous_dir)


def download_binary(tag, args) -> int:
    if Path(tag).is_dir():
        if not args.remove_dir:
            print('Using cached {}'.format(tag))
            return 0
        shutil.rmtree(tag)
    Path(tag).mkdir()
    bin_path = 'releases/download/v{}'.format(tag[1:])
    match = re.compile('v(.*)(rc[0-9]+)$').search(tag)
    if match:
        bin_path = 'releases/download/test.{}'.format(
            match.group(1), match.group(2))
    platform = args.platform
    if tag < "v20" and platform in ["x86_64-apple-darwin", "aarch64-apple-darwin"]:
        platform = "osx64"
    tarball = 'sparkscore-{tag}-{platform}.tar.gz'.format(
        tag=tag[1:], platform=platform)
    tarballUrl = 'https://github.com/sparkspay/sparks/{bin_path}/{tarball}'.format(
        bin_path=bin_path, tarball=tarball)

    print('Fetching: {tarballUrl}'.format(tarballUrl=tarballUrl))

    header, status = subprocess.Popen(
        ['curl', '--head', tarballUrl], stdout=subprocess.PIPE).communicate()
    if re.search("404 Not Found", header.decode("utf-8")):
        print("Binary tag was not found")
        return 1

    curlCmds = [
        ['curl', '-L', '--remote-name', tarballUrl]
    ]

    for cmd in curlCmds:
        ret = subprocess.run(cmd).returncode
        if ret:
            return ret

    hasher = hashlib.sha256()
    with open(tarball, "rb") as afile:
        hasher.update(afile.read())
    tarballHash = hasher.hexdigest()

    if tarballHash not in SHA256_SUMS or SHA256_SUMS[tarballHash] != tarball:
        if tarball in SHA256_SUMS.values():
            print("Checksum did not match")
            return 1

        print("Checksum for given version doesn't exist")
        return 1
    print("Checksum matched")

    # Extract tarball
    # special case for v17 and earlier: other name of version
    filename = tag[1:-2] if tag[1:3] == "0." else tag[1:]
    ret = subprocess.run(['tar', '-zxf', tarball, '-C', tag,
                          '--strip-components=1',
                          'sparkscore-{tag}'.format(tag=filename, platform=args.platform)]).returncode
    if ret != 0:
        print(f"Failed to extract the {tag} tarball")
        return ret

    Path(tarball).unlink()

    if tag >= "v19" and platform == "arm64-apple-darwin":
        # Starting with v23 there are arm64 binaries for ARM (e.g. M1, M2) macs, but they have to be signed to run
        binary_path = f'{os.getcwd()}/{tag}/bin/'

        for arm_binary in os.listdir(binary_path):
            # Is it already signed?
            ret = subprocess.run(
                ['codesign', '-v', binary_path + arm_binary],
                stderr=subprocess.DEVNULL,  # Suppress expected stderr output
            ).returncode
            if ret == 1:
                # Have to self-sign the binary
                ret = subprocess.run(
                    ['codesign', '-s', '-', binary_path + arm_binary]
                ).returncode
                if ret != 0:
                    print(f"Failed to self-sign {tag} {arm_binary} arm64 binary")
                    return 1

                # Confirm success
                ret = subprocess.run(
                    ['codesign', '-v', binary_path + arm_binary]
                ).returncode
                if ret != 0:
                    print(f"Failed to verify the self-signed {tag} {arm_binary} arm64 binary")
                    return 1

    return 0


def build_release(tag, args) -> int:
    githubUrl = "https://github.com/sparkspay/sparks"
    if args.remove_dir:
        if Path(tag).is_dir():
            shutil.rmtree(tag)
    if not Path(tag).is_dir():
        # fetch new tags
        subprocess.run(
            ["git", "fetch", githubUrl, "--tags"])
        output = subprocess.check_output(['git', 'tag', '-l', tag])
        if not output:
            print('Tag {} not found'.format(tag))
            return 1
    ret = subprocess.run([
        'git', 'clone', githubUrl, tag
    ]).returncode
    if ret:
        return ret
    with pushd(tag):
        ret = subprocess.run(['git', 'checkout', tag]).returncode
        if ret:
            return ret
        host = args.host
        if args.depends:
            with pushd('depends'):
                ret = subprocess.run(['make', 'NO_QT=1']).returncode
                if ret:
                    return ret
                host = os.environ.get(
                    'HOST', subprocess.check_output(['./config.guess']))
        config_flags = '--prefix={pwd}/depends/{host} '.format(
            pwd=os.getcwd(),
            host=host) + args.config_flags
        cmds = [
            './autogen.sh',
            './configure {}'.format(config_flags),
            'make',
        ]
        for cmd in cmds:
            ret = subprocess.run(cmd.split()).returncode
            if ret:
                return ret
        # Move binaries, so they're in the same place as in the
        # release download
        Path('bin').mkdir(exist_ok=True)
        files = ['sparksd', 'sparks-cli', 'sparks-tx']
        for f in files:
            Path('src/'+f).rename('bin/'+f)
    return 0


def check_host(args) -> int:
    args.host = os.environ.get('HOST', subprocess.check_output(
        './depends/config.guess').decode())
    if args.download_binary:
        platforms = {
            'aarch64-*-linux*': 'aarch64-linux-gnu',
            'x86_64-*-linux*': 'x86_64-linux-gnu',
            'x86_64-apple-darwin*': 'x86_64-apple-darwin',
            'aarch64-apple-darwin*': 'aarch64-apple-darwin',
        }
        args.platform = ''
        for pattern, target in platforms.items():
            if fnmatch(args.host, pattern):
                args.platform = target
        if not args.platform:
            print('Not sure which binary to download for {}'.format(args.host))
            return 1
    return 0


def main(args) -> int:
    Path(args.target_dir).mkdir(exist_ok=True, parents=True)
    print("Releases directory: {}".format(args.target_dir))
    ret = check_host(args)
    if ret:
        return ret
    if args.download_binary:
        with pushd(args.target_dir):
            for tag in args.tags:
                ret = download_binary(tag, args)
                if ret:
                    return ret
        return 0
    args.config_flags = os.environ.get('CONFIG_FLAGS', '')
    args.config_flags += ' --without-gui --disable-tests --disable-bench'
    with pushd(args.target_dir):
        for tag in args.tags:
            ret = build_release(tag, args)
            if ret:
                return ret
    return 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('-r', '--remove-dir', action='store_true',
                        help='remove existing directory.')
    parser.add_argument('-d', '--depends', action='store_true',
                        help='use depends.')
    parser.add_argument('-b', '--download-binary', action='store_true',
                        help='download release binary.')
    parser.add_argument('-t', '--target-dir', action='store',
                        help='target directory.', default='releases')
    parser.add_argument('tags', nargs='+',
                        help="release tags. e.g.: v19.1.0 v19.0.0-rc.9")
    args = parser.parse_args()
    sys.exit(main(args))
