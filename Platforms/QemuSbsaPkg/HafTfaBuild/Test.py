
import os
import sys
import shutil
import logging
import json
import subprocess
import threading
import datetime
from io import StringIO
import tempfile

from edk2toolext.environment import shell_environment
from edk2toollib.utility_functions import RunCmd

root = logging.getLogger()
root.setLevel(logging.DEBUG)  # or logging.DEBUG

def create_blank_file(file_name: str, size_bytes: int) -> str:
    """
    Create a blank file of `size_bytes` bytes named `file_name` inside a new temporary directory.
    
    Args:
        file_name: Name of the file to create (no path separators).
        size_bytes: Desired file size in bytes (>= 0).
    
    Returns:
        Full path to the created file as a string.
    """
    if not isinstance(size_bytes, int):
        raise TypeError("size_bytes must be an integer")
    if size_bytes < 0:
        raise ValueError("size_bytes must be >= 0")

    # Validate file name
    base_name = os.path.basename(file_name)
    if base_name in ("", ".", ".."):
        raise ValueError("Invalid file name")

    # Create a new temporary directory
    temp_dir = tempfile.mkdtemp(prefix="blankfile_")
    file_path = os.path.join(temp_dir, base_name)

    # Create the file and set its size
    with open(file_path, "wb") as f:
        f.truncate(size_bytes)

    return file_path

def clone_or_update_repo(repo_url, target_dir, commit_hash, repo_name):
    """Clone or update a repository to a specific commit."""
    if os.path.exists(target_dir):
        logging.info(f"{repo_name} repository already exists, updating to specified commit")
        # Fetch latest changes
        ret = RunCmd("git", "fetch", workingdir=target_dir)
        if ret != 0:
            logging.error(f"Failed to fetch {repo_name}")
            return ret
    else:
        logging.info(f"Cloning {repo_name} repository")
        parent_dir = os.path.dirname(target_dir)
        os.makedirs(parent_dir, exist_ok=True)
        ret = RunCmd("git", f"clone {repo_url} {target_dir}")
        if ret != 0:
            logging.error(f"Failed to clone {repo_name}")
            return ret
    
    # Checkout specific commit
    logging.info(f"Checking out {repo_name} at commit {commit_hash}")
    ret = RunCmd("git", f"checkout {commit_hash}", workingdir=target_dir)
    if ret != 0:
        logging.error(f"Failed to checkout {repo_name} at {commit_hash}")
        return ret
    
    return 0

def build_haf_tfa():
    """Main build function for Hafnium and TF-A firmware."""
    logging.info("Starting Hafnium and TF-A build process")

    # Set up paths
    workspace_root = os.getcwd()
    mu_platforms_path = workspace_root
    patch_tfa = True  # Set to True to apply patches, False to skip
    hafnium_path = os.path.join(mu_platforms_path, "Silicon/Arm/HAF")
    tfa_path = os.path.join(mu_platforms_path, "Silicon/Arm/TFA")
    logging.info (hafnium_path)
    build_output_base = os.path.join(mu_platforms_path, "Platforms/QemuSbsaPkg/HafTfaBuild/")

    logging.info("clang version:")
    ret = RunCmd("clang", "--version", workingdir=mu_platforms_path)

    logging.info("clang which:")
    ret = RunCmd("which", "clang", workingdir=mu_platforms_path)

    logging.info (f"Current working directory: {workspace_root}")
    cmd = "git"
    args = f"config --global --add safe.directory {mu_platforms_path}"
    ret = RunCmd(cmd, args, workingdir=mu_platforms_path)
    if ret != 0:
        logging.error("Failed to add safe.directory")
        return ret

    cmd = "git"
    args = "submodule update --init --recursive Silicon/Arm/HAF"
    ret = RunCmd(cmd, args, workingdir=mu_platforms_path)
    if ret != 0:
        logging.error("Failed to init Hafnium repo")
        return ret
    args = "submodule update --init --recursive Silicon/Arm/TFA"
    ret = RunCmd(cmd, args, workingdir=mu_platforms_path)
    if ret != 0:
        logging.error("Failed to init TFA repo")
        return ret

    # cmd = "python"
    # args = "Platforms/QemuSbsaPkg/PlatformBuild.py --setup"
    # ret = RunCmd(cmd, args, workingdir=mu_platforms_path)
    # if ret != 0:
    #     logging.error("Failed to PlatformBuild.py --setup")
    #     return ret

    # cmd = "python"
    # args = "Platforms/QemuSbsaPkg/PlatformBuild.py --update"
    # ret = RunCmd(cmd, args, workingdir=mu_platforms_path)
    # if ret != 0:
    #     logging.error("Failed to PlatformBuild.py --update")
    #     return ret

    # cmd = "python"
    # args = "Platforms/QemuSbsaPkg/PlatformBuild.py --clean"
    # ret = RunCmd(cmd, args, workingdir=mu_platforms_path)
    # if ret != 0:
    #     logging.error("Failed to PlatformBuild.py --clean")
    #     return ret

    # Copy mu directory from mu_tiano_platforms
    src_mu_dir = os.path.join(mu_platforms_path, "Platforms/QemuSbsaPkg/mu")
    if not os.path.exists(src_mu_dir):
        logging.error(f"Source mu directory not found at {src_mu_dir}")
        return -1
    
    dest_dir = os.path.join(mu_platforms_path, "Silicon/Arm/HAF/project/mu")
    
    # Remove the destination directory if it exists
    if os.path.exists(dest_dir):
        logging.info(f"Removing existing directory: {dest_dir}")
        shutil.rmtree(dest_dir)
    
    # Copy the mu directory and its contents
    logging.info("Copying mu directory to Silicon/Arm/HAF/project")
    shutil.copytree(src_mu_dir, dest_dir)

    # Build Hafnium
    logging.info("Building Hafnium")
    
    hafnium_build_dir = os.path.join(hafnium_path, "out/mu/secure_qemu_aarch64_clang")
    cmd = "make"
    args = "PROJECT=mu PLATFORM=secure_qemu_aarch64"
    ret = RunCmd(cmd, args, workingdir=hafnium_path)
    if ret != 0:
        logging.error("Failed to build Hafnium")
        return ret

    hafnium_binary = os.path.join(hafnium_build_dir, "hafnium.bin")
    if not os.path.exists(hafnium_binary):
        logging.error(f"Hafnium binary not found at {hafnium_binary}")
        return -1

    logging.info(f"Hafnium binary built at: {hafnium_binary}")

    # Building TF-A
    logging.info("Building TF-A")
    
    # shell_environment.CheckpointBuildVars()  # checkpoint our config before we mess with it
    
    # Handle toolchain setup
    if os.environ.get("TOOL_CHAIN_TAG") == "CLANGPDB":
        if os.name == 'nt':
            # Windows-specific setup
            shell_environment.GetEnvironment().set_path('')
            self.InjectVcVarsOfInterests(["LIB", "Path"])
            
            clang_exe = "clang.exe"
            choco_path = shell_environment.GetEnvironment().get_shell_var("CHOCOLATEYINSTALL")
            shell_environment.GetEnvironment().insert_path(os.path.join(choco_path, "bin"))
            shell_environment.GetEnvironment().insert_path(shell_environment.GetEnvironment().get_shell_var("CLANG_BIN"))
            
            # Build fiptool separately for Windows
            cmd = "make"
            args = " fiptool MAKEFLAGS= LIB=\"" + shell_environment.GetEnvironment().get_shell_var("LIB") + "\""
            ret = RunCmd(cmd, args, workingdir=tfa_path)
            if ret != 0:
                return ret
        else:
            clang_exe = "clang"

    # Create SP layout JSON file
    sp_layout_file = os.path.join(build_output_base, 'sp_layout.json')
    with open(sp_layout_file, 'w') as f:
        data = {
            "stmm": {
                "image": {
                    "file": create_blank_file("BL32_AP_MM.fd", 2621440),
                    "offset": "0x2000"
                },
                "pm": {
                    "file": os.path.join(mu_platforms_path, "Platforms/QemuSbsaPkg/fdts/qemu_sbsa_stmm_config.dts"),
                    "offset": "0x1000"
                },
                "package": "tl_pkg",
                "uuid": "eaba83d8-baaf-4eaf-8144-f7fdcbe544a7",
                "owner": "Plat",
                "size": "0x300000"
            },
            "mssp": {
                "image": {
                    "file": create_blank_file("BL32_AP_MM_SP1.fd", 81920),
                    "offset": "0x10000"
                },
                "pm": {
                    "file": os.path.join(mu_platforms_path, "Platforms/QemuSbsaPkg/fdts/qemu_sbsa_mssp_config.dts"),
                    "offset": "0x1000"
                },
                "uuid": "b8bcbd0c-8e8f-4ebe-99eb-3cbbdd0cd412",
                "owner": "Plat"
            }
        }
        json.dump(data, f, indent=4)

    # Prepare TF-A build arguments
    cmd = "make"
    # if self.env.GetValue("TOOL_CHAIN_TAG") == "CLANGPDB":
    #     args = f"CC={clang_exe}"
    # elif self.env.GetValue("TOOL_CHAIN_TAG") == "GCC5":
    #     args = "CROSS_COMPILE=" + shell_environment.GetEnvironment().get_shell_var("GCC5_AARCH64_PREFIX")
    #     args += " -j $(nproc)"
    # else:
    #     logging.error("Unsupported toolchain")
    #     return -1
    args = f"CC=clang"
    args = "CROSS_COMPILE=" + os.environ.get("GCC5_AARCH64_PREFIX")
    args += " -j $(nproc)"
    args += " PLAT=" + "qemu_sbsa"
    args += " ARCH=" + "AARCH64".lower()
    args += " DEBUG=" + str(1)
    args += " ENABLE_SME_FOR_SWD=1 ENABLE_SVE_FOR_SWD=1 ENABLE_SME_FOR_NS=1 ENABLE_SVE_FOR_NS=1"
    args += f" SPD=spmd SPMD_SPM_AT_SEL2=1 SP_LAYOUT_FILE={sp_layout_file}"
    args += " ENABLE_FEAT_HCX=1 HOB_LIST=1 TRANSFER_LIST=1 LOG_LEVEL=40"
    args += f" BL32={hafnium_binary}"
    args += " all fip"

    # # Apply patches if needed
    # patch_tfa = (self.env.GetValue("PATCH_TFA", "TRUE").upper() == "TRUE")
    if patch_tfa:
        outstream = StringIO()
        ret = RunCmd("git", "rev-parse HEAD", outstream=outstream, workingdir=tfa_path)
        if ret != 0:
            logging.error("Failed to get git HEAD for TFA")
            return ret
        arm_tfa_git_head = outstream.getvalue().strip()
        logging.info(f"TFA HEAD before patches: {arm_tfa_git_head}")
        
        patches = os.path.join(workspace_root, "Platforms/QemuSbsaPkg/tfa_patches/*.patch")
        ret = RunCmd("git", f"am {patches}", workingdir=tfa_path)
        if ret != 0:
            logging.error("Failed to apply patches, continuing without them")
            # Reset to clean state
            RunCmd("git", f"checkout {arm_tfa_git_head}", workingdir=tfa_path)
            patch_tfa = False

    # Create and run build script
    temp_bash = os.path.join(tfa_path, "temp_tfa_build.sh")
    with open(temp_bash, "w") as f:
        f.write("#!/bin/bash\n")
        f.write("poetry --verbose install\n")
        f.write("poetry env activate\n")
        f.write("poetry show\n")
        f.write(f"{cmd} {args}\n")

    # Execute the build
    cached_environ = os.environ.copy()
    ret = RunCmd("bash", temp_bash, workingdir=tfa_path)

    # Revert patches if applied
    if patch_tfa:
        revert_ret = RunCmd("git", f"checkout {arm_tfa_git_head}", workingdir=tfa_path)
        if revert_ret != 0:
            logging.error("Failed to revert TFA patches")
            return revert_ret

    if ret != 0:
        logging.error("TF-A build failed")
    logging.info("TF-A build completed successfully")

    # Remove temp script
    os.remove(temp_bash)
    os.remove(sp_layout_file)

    # Copy output binaries to a known location
    output_dir = os.path.join(build_output_base, "firmware_binaries")
    os.makedirs(output_dir, exist_ok=True)

    # Copy Hafnium binary
    hafnium_output = os.path.join(output_dir, "hafnium.bin")
    shutil.copy2(hafnium_binary, hafnium_output)
    logging.info(f"Hafnium binary saved to: {hafnium_output}")
    
    # Copy TF-A binaries
    tfa_build_dir = os.path.join(tfa_path, "build", "qemu_sbsa", "debug")

    # Copy BL1 (if exists)
    bl1_src = os.path.join(tfa_build_dir, "bl1.bin")
    if os.path.exists(bl1_src):
        bl1_output = os.path.join(output_dir, "bl1.bin")
        shutil.copy2(bl1_src, bl1_output)
        logging.info(f"BL1 binary saved to: {bl1_output}")
    
    # Copy BL2 (if exists)
    bl2_src = os.path.join(tfa_build_dir, "bl2.bin")
    if os.path.exists(bl2_src):
        bl2_output = os.path.join(output_dir, "bl2.bin")
        shutil.copy2(bl2_src, bl2_output)
        logging.info(f"BL2 binary saved to: {bl2_output}")
    
    # Copy BL31 (TF-A runtime)
    bl31_src = os.path.join(tfa_build_dir, "bl31.bin")
    if os.path.exists(bl31_src):
        bl31_output = os.path.join(output_dir, "bl31.bin")
        shutil.copy2(bl31_src, bl31_output)
        logging.info(f"BL31 (TF-A) binary saved to: {bl31_output}")

    # Copy FIP binary
    fip_src = os.path.join(tfa_build_dir, "fip.bin")
    if os.path.exists(fip_src):
        fip_output = os.path.join(output_dir, "fip.bin")
        shutil.copy2(fip_src, fip_output)
        logging.info(f"FIP binary saved to: {fip_output}")
    else:
        logging.error(f"FIP binary not found at {fip_src}")
        return -1
    
    logging.info(f"All firmware binaries saved to: {output_dir}")

    return 0

def main():
    build_haf_tfa()

if __name__ == "__main__":
    main()
